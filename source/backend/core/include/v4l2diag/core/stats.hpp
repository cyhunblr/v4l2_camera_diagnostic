#pragma once

#include <cstddef>
#include <vector>

namespace v4l2diag {

struct Stats {
  double mean = 0.0;
  double stddev = 0.0;
  double min = 0.0;
  double max = 0.0;
  double median = 0.0;
  double p5 = 0.0;
  double p95 = 0.0;
  double p99 = 0.0;
  double jitter = 0.0;
  std::size_t count = 0;
  std::size_t outliers = 0;
};

Stats compute_stats(const std::vector<double> &data);

}  // namespace v4l2diag
