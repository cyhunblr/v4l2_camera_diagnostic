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

WarmupResult find_warmup_frame(const std::vector<double> &latencies, double steady_reference_ms,
                               double stability_threshold_pct) {
  WarmupResult result;

  // Judging against a reference of zero is judging against nothing. Saying so is the
  // whole point: the alternative is a frame number that reads like a measurement.
  if (!(steady_reference_ms > 0.0)) {
    result.censor = WarmupResult::Censor::NoReference;
    return result;
  }

  std::size_t good = 0;
  for (double latency : latencies) {
    if (latency > 0.0) {
      ++good;
    }
  }
  // Three good frames is the minimum that can show a trend rather than a pair of points.
  if (good < 3) {
    result.censor = WarmupResult::Censor::TooFewFrames;
    return result;
  }

  const double tolerance = steady_reference_ms * (stability_threshold_pct / 100.0);
  for (std::size_t start = 0; start < latencies.size(); ++start) {
    if (latencies[start] <= 0.0) {
      continue;  // a missed frame cannot be the moment latency settled
    }
    bool settled = true;
    for (std::size_t at = start; at < latencies.size(); ++at) {
      if (latencies[at] <= 0.0) {
        continue;
      }
      if (std::abs(latencies[at] - steady_reference_ms) > tolerance) {
        settled = false;
        break;
      }
    }
    if (settled) {
      result.stabilized = true;
      result.frame = static_cast<int>(start);
      result.censor = WarmupResult::Censor::None;
      return result;
    }
  }

  // Latency was still moving when the window ended. There is no warm-up length to
  // report -- the observation window was too short, and how much too short is unknown.
  result.censor = WarmupResult::Censor::NeverSettled;
  return result;
}

}  // namespace v4l2diag
