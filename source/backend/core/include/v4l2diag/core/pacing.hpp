#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <iomanip>
#include <sstream>
#include <string>
#include <thread>

namespace v4l2diag {

// Wall-clock gap left between two consecutive tests.
//
// Cameras behind a shared deserializer re-initialise the whole camera group
// over I2C on every STREAMON — measured at ~1130ms on the tier4_isx021 boards
// this suite targets. Each test opens and closes the device itself, so without
// a gap the suite drives that re-initialisation back-to-back for the length of
// a run. This is the settling time the hardware and the kernel get in between.
constexpr double kInterTestCooldownSec = 5.0;

// How often a cancellable sleep wakes to re-check its stop token. Short enough
// that a stop request during a five-second cooldown still feels immediate.
constexpr int kStopPollMs = 100;

// Sleeps for `seconds`, waking every kStopPollMs to re-check `stop_token`.
// Returns false if it returned early because the token was set, true if the
// full duration elapsed. A plain blocking sleep here would leave a stop
// request unanswered for the whole cooldown.
//
// A null token means "not cancellable" and simply sleeps the full duration.
inline bool interruptible_sleep(double seconds, const std::atomic<bool> *stop_token) {
  if (seconds <= 0.0) {
    return !(stop_token != nullptr && stop_token->load(std::memory_order_relaxed));
  }
  const auto deadline =
      std::chrono::steady_clock::now() +
      std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(seconds));
  while (true) {
    if (stop_token != nullptr && stop_token->load(std::memory_order_relaxed)) {
      return false;
    }
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) {
      return true;
    }
    const auto remaining_ms = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
    const auto slice_ms = std::min<long long>(remaining_ms + 1, kStopPollMs);
    std::this_thread::sleep_for(std::chrono::milliseconds(slice_ms));
  }
}

// Paces a capture loop to the interval derived from the profile's
// trigger_rate_hz (injected into each test's params as __trigger_interval_ms by
// run_test).
//
// The loops this replaces slept a fixed 100ms *after* an already-blocking
// capture, so the real period was capture time + 100ms — 155ms measured on
// hardware whose profile asked for 10Hz. Sleeping only the remainder of the
// interval makes the configured rate the rate that actually runs.
//
// What that interval MEANS depends on the trigger mode, and the distinction
// matters for how a shortfall is reported:
//
//  - Triggered: trigger_rate_hz sets how often the trigger fires, so it really
//    is the capture rate being requested.
//  - Free-run: nothing in this codebase sets the camera's frame rate (there is
//    no VIDIOC_S_PARM call anywhere). The camera streams at its own pace and the
//    interval only throttles how often the test reads a frame.
//
// An iteration that outlasts the interval cannot be paced, so it is counted
// instead of silently absorbed: a loop running at 6Hz while its report says 10Hz
// is exactly the failure this class exists to prevent.
class Pacer {
 public:
  // `externally_triggered` is false in free-run mode. The pacing maths is
  // identical either way; only the explanation differs. Reporting a free-run
  // shortfall as a missed "trigger rate" would point the reader at a trigger
  // that never fired and at a setting that cannot fix it.
  explicit Pacer(double interval_ms, bool externally_triggered = true)
      : interval_ms_(interval_ms > 0.0 ? interval_ms : 0.0), externally_triggered_(externally_triggered) {}

  // Call at the top of each loop iteration.
  void begin() {
    start_ = std::chrono::steady_clock::now();
    started_ = true;
    iterations_++;
  }

  // Call at the bottom: sleeps whatever is left of the interval. Without a
  // preceding begin() there is no iteration to pace, so this does nothing.
  void wait() {
    if (!started_) {
      return;
    }
    started_ = false;
    const double elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start_).count();
    const double remaining = interval_ms_ - elapsed;
    if (remaining <= 0.0) {
      overruns_++;
      worst_overrun_ms_ = std::max(worst_overrun_ms_, -remaining);
      // An overrunning iteration's period is however long it actually took; a
      // paced one's is the interval. Accumulating both gives the rate the loop
      // really ran at, which is the number worth reporting.
      total_period_ms_ += elapsed;
      return;
    }
    total_period_ms_ += interval_ms_;
    std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(remaining));
  }

  // The rate the loop actually achieved, in Hz. Zero before any iteration.
  double actual_hz() const {
    if (iterations_ == 0 || total_period_ms_ <= 0.0) {
      return 0.0;
    }
    return 1000.0 / (total_period_ms_ / static_cast<double>(iterations_));
  }

  double interval_ms() const {
    return interval_ms_;
  }
  int iterations() const {
    return iterations_;
  }
  int overruns() const {
    return overruns_;
  }
  double worst_overrun_ms() const {
    return worst_overrun_ms_;
  }

  // A TestResult::notes entry when the loop could not hold its interval, empty
  // otherwise. Reporting this matters more than the pacing itself: it is the
  // difference between a measured rate and an assumed one, and without it a
  // reader takes the configured rate for the rate that ran.
  //
  // The two modes describe genuinely different situations, so they get different
  // text rather than one hedged sentence:
  //
  //  - Triggered: trigger_rate_hz drives the trigger, so asking for a rate the
  //    hardware cannot deliver is a real misconfiguration with a real fix.
  //  - Free-run: nothing sets the camera's frame rate — there is no S_PARM call
  //    anywhere in this codebase. The camera streams at whatever rate it streams
  //    at, and trigger_rate_hz only throttles how often the test reads a frame.
  //    So this is never "a rate we asked for and missed"; it is the loop
  //    discovering that reads take longer than the throttle it was given.
  std::string overrun_note() const {
    if (overruns_ == 0 || interval_ms_ <= 0.0) {
      return std::string();
    }
    std::ostringstream note;
    note << std::fixed;

    if (!externally_triggered_) {
      note << "Frames arrived more slowly than this test polled for them. In free-run nothing sets the camera's frame "
              "rate — it streams at its own pace, and trigger_rate_hz only limits how often the test reads a frame. "
              "That limit was "
           << std::setprecision(1) << interval_ms_ << " ms per read (" << std::setprecision(2)
           << (1000.0 / interval_ms_) << " Hz), but " << overruns_ << " of " << iterations_
           << " reads took longer than that (the slowest by " << std::setprecision(1) << worst_overrun_ms_
           << " ms), so the loop ran at about " << std::setprecision(2) << actual_hz()
           << " Hz instead. That is the camera's delivery rate, not a fault. Per-frame latency figures remain valid.";
      return note.str();
    }

    note << "Capture could not keep up with the configured trigger rate. Asked for " << std::setprecision(2)
         << (1000.0 / interval_ms_) << " Hz (" << std::setprecision(1) << interval_ms_
         << " ms per frame); the loop actually ran at about " << std::setprecision(2) << actual_hz() << " Hz, because "
         << overruns_ << " of " << iterations_ << " frames took longer than that interval (the slowest by "
         << std::setprecision(1) << worst_overrun_ms_
         << " ms). Per-frame latency figures remain valid, but they were sampled at the slower rate above, not at the "
            "configured one. To measure at the configured rate, lower trigger_rate_hz in the device profile to a "
            "value this camera can sustain.";
    return note.str();
  }

 private:
  double interval_ms_;
  bool externally_triggered_ = true;
  std::chrono::steady_clock::time_point start_{};
  bool started_ = false;
  int iterations_ = 0;
  int overruns_ = 0;
  double worst_overrun_ms_ = 0.0;
  double total_period_ms_ = 0.0;
};

}  // namespace v4l2diag
