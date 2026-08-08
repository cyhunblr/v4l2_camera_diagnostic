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

// The outcome of looking for the frame at which capture latency settles.
//
// `stabilized` is the distinction that matters: a cycle that never settled inside the
// observation window has NO warm-up length, and `frame` must not be read. The runner used
// to leave `frame` at max_frames_per_cycle in that case, so the report showed the
// PARAMETER (30) as if it were a measurement -- the real answer could have been 31 or 500.
struct WarmupResult {
  bool stabilized = false;
  // Frames before latency settled. Meaningful only when `stabilized` is true.
  int frame = 0;
  // Why a cycle produced no measurement, for the report's Detail cell.
  enum class Censor {
    None,          // stabilized
    NeverSettled,  // latency was still moving at the end of the window
    NoReference,   // the steady-state reference itself was unusable
    TooFewFrames,  // not enough good frames to judge
  };
  Censor censor = Censor::NeverSettled;
};

// Finds the first frame from which every later good frame stays within
// `stability_threshold_pct` of `steady_reference_ms`.
//
// The reference is a PARAMETER, not taken from the tail of `latencies`. Deriving it from
// the same window it judges makes two different situations indistinguishable: "never
// settled" and "settled, but the last frames were noisy so the reference is wrong". A
// caller that has no independent reference passes 0 and gets `NoReference` back rather
// than a number that looks measured.
//
// A negative latency marks a missed frame and is skipped, never counted as settled.
WarmupResult find_warmup_frame(const std::vector<double> &latencies, double steady_reference_ms,
                               double stability_threshold_pct);

}  // namespace v4l2diag
