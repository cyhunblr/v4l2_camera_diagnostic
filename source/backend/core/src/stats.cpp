#include "v4l2diag/core/stats.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

namespace v4l2diag {

Stats compute_stats(const std::vector<double> &data) {
  Stats s;
  s.count = data.size();
  if (s.count == 0) {
    return s;
  }

  std::vector<double> sorted = data;
  std::sort(sorted.begin(), sorted.end());

  s.min = sorted.front();
  s.max = sorted.back();
  s.mean = std::accumulate(sorted.begin(), sorted.end(), 0.0) / static_cast<double>(s.count);

  if (s.count % 2 == 0) {
    s.median = (sorted[s.count / 2 - 1] + sorted[s.count / 2]) / 2.0;
  } else {
    s.median = sorted[s.count / 2];
  }

  const auto percentile = [&](double fraction) {
    std::size_t idx = static_cast<std::size_t>(static_cast<double>(s.count) * fraction);
    idx = std::min(idx, s.count - 1);
    return sorted[idx];
  };

  s.p5 = percentile(0.05);
  s.p95 = percentile(0.95);
  s.p99 = percentile(0.99);

  if (s.count > 1) {
    const double sum_sq = std::accumulate(data.begin(), data.end(), 0.0, [&](double acc, double v) {
      const double delta = v - s.mean;
      return acc + delta * delta;
    });
    s.stddev = std::sqrt(sum_sq / static_cast<double>(s.count - 1));
  }

  if (s.stddev > 0.0) {
    for (double v : data) {
      if (std::abs(v - s.mean) > 3.0 * s.stddev) {
        ++s.outliers;
      }
    }
  }

  if (s.count > 1) {
    std::vector<double> deltas;
    deltas.reserve(data.size() - 1);
    for (std::size_t i = 1; i < data.size(); ++i) {
      deltas.push_back(data[i] - data[i - 1]);
    }
    const double mean_delta = std::accumulate(deltas.begin(), deltas.end(), 0.0) / deltas.size();
    const double delta_sq = std::accumulate(deltas.begin(), deltas.end(), 0.0, [&](double acc, double v) {
      const double delta = v - mean_delta;
      return acc + delta * delta;
    });
    s.jitter = std::sqrt(delta_sq / static_cast<double>(deltas.size()));
  }

  return s;
}

}  // namespace v4l2diag
