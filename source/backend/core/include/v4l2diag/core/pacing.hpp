#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
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

// Paces a capture loop to the profile's configured trigger interval
// (1000 / trigger_rate_hz, injected into each test's params as
// __trigger_interval_ms by run_test).
//
// The loops this replaces slept a fixed 100ms *after* an already-blocking
// capture, so the real period was capture time + 100ms — 155ms measured on
// hardware whose profile asked for 10Hz. Sleeping only the remainder of the
// interval makes the configured rate the rate that actually runs.
//
// An iteration that outlasts the interval cannot be paced, so it is counted
// instead of silently absorbed: a loop free-running at 6Hz while its report
// says 10Hz is exactly the failure this class exists to prevent.
class Pacer {
 public:
  explicit Pacer(double interval_ms) : interval_ms_(interval_ms > 0.0 ? interval_ms : 0.0) {}

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
      return;
    }
    std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(remaining));
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

  // A TestResult::details line when the loop could not hold the configured
  // rate, empty otherwise. Reporting this matters more than the pacing itself:
  // it is the difference between a measured rate and an assumed one.
  std::string overrun_note() const {
    if (overruns_ == 0 || interval_ms_ <= 0.0) {
      return std::string();
    }
    char buf[224];
    snprintf(buf, sizeof(buf),
             "Trigger rate not held: %d/%d iterations outran the %.1fms interval (worst by %.1fms) — the loop "
             "free-ran instead of pacing to %.2fHz. Lower trigger_rate_hz in the profile to match the hardware.",
             overruns_, iterations_, interval_ms_, worst_overrun_ms_, 1000.0 / interval_ms_);
    return std::string(buf);
  }

 private:
  double interval_ms_;
  std::chrono::steady_clock::time_point start_{};
  bool started_ = false;
  int iterations_ = 0;
  int overruns_ = 0;
  double worst_overrun_ms_ = 0.0;
};

}  // namespace v4l2diag
