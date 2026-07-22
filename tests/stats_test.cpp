#include "v4l2diag/core/stats.hpp"

#include <cmath>
#include <iostream>
#include <vector>

namespace {

bool near(double a, double b) {
  return std::abs(a - b) < 1e-9;
}

}  // namespace

int main() {
  const std::vector<double> data{1.0, 2.0, 3.0, 4.0, 5.0};
  const auto stats = v4l2diag::compute_stats(data);

  if (stats.count != 5 || !near(stats.mean, 3.0) || !near(stats.median, 3.0) || !near(stats.min, 1.0) ||
      !near(stats.max, 5.0)) {
    std::cerr << "unexpected basic statistics\n";
    return 1;
  }

  const auto empty = v4l2diag::compute_stats({});
  if (empty.count != 0 || empty.mean != 0.0) {
    std::cerr << "unexpected empty statistics\n";
    return 1;
  }

  return 0;
}
