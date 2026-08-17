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

  // --- find_warmup_frame: a cycle that never settles has NO warm-up length ---
  //
  // This is the defect the function exists to prevent. The runner used to leave
  // warmup_frame at max_frames_per_cycle when nothing settled, so the report printed the
  // PARAMETER as a measurement: 30 frames, when the truth could be 31 or 500.
  {
    // Latency that keeps climbing: never within threshold of the reference.
    const std::vector<double> climbing = {10, 20, 30, 40, 50, 60, 70, 80};
    const auto never = v4l2diag::find_warmup_frame(climbing, 10.0, 5.0);
    if (never.stabilized) {
      std::cerr << "a cycle that never settled was reported as stabilized\n";
      return 1;
    }
    if (never.censor != v4l2diag::WarmupResult::Censor::NeverSettled) {
      std::cerr << "the reason a cycle produced no measurement is wrong\n";
      return 1;
    }
  }

  // A cycle that settles reports WHERE it settled, counted from the first frame.
  {
    // Three noisy frames, then steady at 100.
    const std::vector<double> settles = {180, 150, 120, 100, 101, 99, 100, 100};
    const auto found = v4l2diag::find_warmup_frame(settles, 100.0, 5.0);
    if (!found.stabilized) {
      std::cerr << "a cycle that clearly settles was not detected\n";
      return 1;
    }
    if (found.frame != 3) {
      std::cerr << "warm-up frame is " << found.frame << ", expected 3\n";
      return 1;
    }
  }

  // No reference means no measurement -- not a number that looks measured.
  {
    const std::vector<double> steady = {100, 100, 100, 100, 100};
    const auto no_ref = v4l2diag::find_warmup_frame(steady, 0.0, 5.0);
    if (no_ref.stabilized || no_ref.censor != v4l2diag::WarmupResult::Censor::NoReference) {
      std::cerr << "a missing steady-state reference produced a measurement\n";
      return 1;
    }
  }

  // Missed frames (negative latency) are skipped, never counted as settled.
  {
    const std::vector<double> with_misses = {-1, -1, 100, 100, 100, 100};
    const auto found = v4l2diag::find_warmup_frame(with_misses, 100.0, 5.0);
    if (!found.stabilized || found.frame != 2) {
      std::cerr << "missed frames were not skipped correctly (frame " << found.frame << ")\n";
      return 1;
    }
  }

  // Too few good frames to judge: stated, not guessed.
  {
    const std::vector<double> tiny = {100, 100};
    const auto few = v4l2diag::find_warmup_frame(tiny, 100.0, 5.0);
    if (few.stabilized || few.censor != v4l2diag::WarmupResult::Censor::TooFewFrames) {
      std::cerr << "a window too short to judge produced a measurement\n";
      return 1;
    }
  }

  return 0;
}
