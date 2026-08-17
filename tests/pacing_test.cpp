#include "v4l2diag/core/pacing.hpp"

#include "v4l2diag/core/threshold_registry.hpp"

#include <atomic>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

namespace {

bool require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << message << "\n";
  }
  return condition;
}

double elapsed_ms_since(std::chrono::steady_clock::time_point t0) {
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

// Timing assertions need slack: a sleep may overshoot under load, and the CI
// machine is not realtime. Every bound below is one-sided and generous, so the
// test fails on a wrong *shape* (no sleep at all, sleeping the full interval on
// top of the work, ignoring the stop token) rather than on jitter.
constexpr double kSlackMs = 400.0;

}  // namespace

int main() {
  bool ok = true;

  // A loop whose work is shorter than the interval should be paced to the
  // interval, not to work + interval. This is the regression the Pacer exists
  // for: the fixed sleep it replaces made a 10Hz profile run at 6.45Hz.
  //
  // The numbers are chosen so the two behaviours cannot overlap. 5 iterations
  // of 80ms work at a 100ms interval is 500ms when paced and 5*(80+100)=900ms
  // with the old fixed sleep; the upper bound sits at 700ms, clear of both.
  {
    v4l2diag::Pacer pacer(100.0);
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 5; i++) {
      pacer.begin();
      std::this_thread::sleep_for(std::chrono::milliseconds(80));  // "capture"
      pacer.wait();
    }
    const double total = elapsed_ms_since(t0);
    ok &= require(total >= 490.0, "5 iterations at a 100ms interval should take at least ~500ms, took " +
                                      std::to_string(total) + "ms");
    ok &= require(total < 700.0, "pacing added the work on top of the interval: took " + std::to_string(total) +
                                     "ms (paced is ~500ms, the old fixed sleep would be ~900ms)");
    ok &= require(pacer.overruns() == 0, "80ms of work inside a 100ms interval must not count as an overrun");
    ok &= require(pacer.iterations() == 5, "iteration count should track begin() calls");
    ok &= require(pacer.overrun_note().empty(), "a loop that held its rate must not emit an overrun note");
  }

  // Work longer than the interval cannot be paced. It must not sleep, and it
  // must be reported — a loop free-running at a rate its report calls 10Hz is
  // the exact failure this class guards against.
  {
    v4l2diag::Pacer pacer(20.0);
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 2; i++) {
      pacer.begin();
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      pacer.wait();
    }
    const double total = elapsed_ms_since(t0);
    ok &= require(total < 100.0 + kSlackMs,
                  "an overrunning loop must not sleep on top of its work, took " + std::to_string(total) + "ms");
    ok &= require(pacer.overruns() == 2, "both iterations outran the interval, got " + std::to_string(pacer.overruns()));
    ok &= require(pacer.worst_overrun_ms() >= 20.0,
                  "worst overrun should be at least 50-20=30ms, got " + std::to_string(pacer.worst_overrun_ms()));
    const std::string note = pacer.overrun_note();
    ok &= require(!note.empty(), "an overrunning loop must emit a note");
    ok &= require(note.find("2 of 2") != std::string::npos,
                  "the note should say how many frames overran: " + note);
    // The note exists so a reader learns the rate that actually ran, not just
    // that something was missed. 50ms per frame is ~20Hz.
    ok &= require(note.find("20.0") != std::string::npos || note.find("19.9") != std::string::npos,
                  "the note should report the achieved rate (~20Hz): " + note);
    ok &= require(note.find("trigger rate") != std::string::npos,
                  "a triggered loop's note should name the trigger rate: " + note);
    ok &= require(note.find("trigger_rate_hz") != std::string::npos,
                  "the note should name the setting to change: " + note);
    // actual_hz() is what feeds that sentence; 50ms per frame is ~20Hz.
    ok &= require(pacer.actual_hz() > 17.0 && pacer.actual_hz() < 23.0,
                  "actual_hz should reflect the real period, got " + std::to_string(pacer.actual_hz()));
  }

  // Free-run fires no trigger, so a shortfall there is the camera's own rate,
  // not a missed trigger. The note must not blame a trigger that never ran.
  {
    v4l2diag::Pacer pacer(20.0, /*externally_triggered=*/false);
    for (int i = 0; i < 2; i++) {
      pacer.begin();
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      pacer.wait();
    }
    const std::string note = pacer.overrun_note();
    ok &= require(!note.empty(), "free-run overruns are still worth reporting");
    ok &= require(note.find("free-run") != std::string::npos, "the free-run note should say so: " + note);
    ok &= require(note.find("trigger rate") == std::string::npos,
                  "free-run has no trigger rate to miss, so the note must not claim one: " + note);
    // Nothing configures the camera's rate in free-run — there is no S_PARM call
    // in the codebase — so the note must not frame this as a rate that was
    // requested and missed.
    ok &= require(note.find("Asked for") == std::string::npos,
                  "free-run sets no frame rate, so the note must not say a rate was asked for: " + note);
    ok &= require(note.find("lower trigger_rate_hz") == std::string::npos &&
                      note.find("To measure at the configured rate") == std::string::npos,
                  "free-run cannot fix this by changing trigger_rate_hz, so must not advise it: " + note);
    ok &= require(note.find("not a fault") != std::string::npos,
                  "free-run should say the slower rate is the camera's own, not a fault: " + note);
  }

  // A loop that held its rate reports the configured rate back through
  // actual_hz(), so callers can compare the two without special-casing.
  {
    v4l2diag::Pacer pacer(50.0);
    for (int i = 0; i < 3; i++) {
      pacer.begin();
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      pacer.wait();
    }
    ok &= require(pacer.actual_hz() > 18.0 && pacer.actual_hz() < 22.0,
                  "a paced 50ms loop should report ~20Hz, got " + std::to_string(pacer.actual_hz()));
    ok &= require(pacer.overrun_note().empty(), "a loop that held its rate emits no note");
  }

  // wait() without begin() has no iteration to pace and must be a no-op rather
  // than sleeping against an uninitialised start point.
  {
    v4l2diag::Pacer pacer(500.0);
    const auto t0 = std::chrono::steady_clock::now();
    pacer.wait();
    ok &= require(elapsed_ms_since(t0) < kSlackMs, "wait() without begin() must not sleep");
    ok &= require(pacer.iterations() == 0, "wait() without begin() must not count an iteration");
  }

  // A non-positive interval means "no pacing configured"; it must degrade to
  // not sleeping rather than to negative-duration arithmetic.
  {
    v4l2diag::Pacer pacer(0.0);
    pacer.begin();
    const auto t0 = std::chrono::steady_clock::now();
    pacer.wait();
    ok &= require(elapsed_ms_since(t0) < kSlackMs, "a zero interval must not sleep");
    ok &= require(pacer.overrun_note().empty(), "a zero interval is not a rate to miss, so it emits no note");
  }

  // The cooldown sleep must run its full duration when nothing cancels it.
  {
    const auto t0 = std::chrono::steady_clock::now();
    const bool completed = v4l2diag::interruptible_sleep(0.3, nullptr);
    const double total = elapsed_ms_since(t0);
    ok &= require(completed, "an uncancelled sleep should report completion");
    ok &= require(total >= 290.0, "sleep returned early at " + std::to_string(total) + "ms");
    ok &= require(total < 300.0 + kSlackMs, "sleep overshot badly at " + std::to_string(total) + "ms");
  }

  // A token already set must return immediately: the five-second cooldown sits
  // between every pair of tests, and a stop request cannot wait it out.
  {
    std::atomic<bool> stop{true};
    const auto t0 = std::chrono::steady_clock::now();
    const bool completed = v4l2diag::interruptible_sleep(5.0, &stop);
    const double total = elapsed_ms_since(t0);
    ok &= require(!completed, "a cancelled sleep should report that it did not complete");
    ok &= require(total < kSlackMs, "a pre-set stop token should return at once, took " + std::to_string(total) + "ms");
  }

  // A token set partway through must cut the sleep short, not merely be noticed
  // after it ends.
  {
    std::atomic<bool> stop{false};
    std::thread setter([&stop] {
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
      stop.store(true, std::memory_order_relaxed);
    });
    const auto t0 = std::chrono::steady_clock::now();
    const bool completed = v4l2diag::interruptible_sleep(5.0, &stop);
    const double total = elapsed_ms_since(t0);
    setter.join();
    ok &= require(!completed, "a sleep cancelled mid-flight should report that it did not complete");
    ok &= require(total < 200.0 + kSlackMs,
                  "cancellation should be noticed within one poll slice, took " + std::to_string(total) + "ms");
  }

  // t06's rapid loop needs at least as many warmup pulses as t03 measures to
  // first frame. One pulse leaves the measured capture's own pulse priming the
  // pipeline, which times out every cycle and reports 0/N on healthy hardware.
  {
    const auto params = v4l2diag::default_test_params();
    const auto test = params.find("t06-stream-cycles");
    ok &= require(test != params.end(), "t06-stream-cycles should have default params");
    if (test != params.end()) {
      const auto warmup = test->second.find("rapid_warmup");
      ok &= require(warmup != test->second.end(), "t06-stream-cycles should define rapid_warmup");
      if (warmup != test->second.end()) {
        ok &= require(warmup->second >= 2.0, "rapid_warmup must cover t03's trigger_pulses_to_first_frame (>=2), got " +
                                                 std::to_string(warmup->second));
      }
    }
  }

  // resolve() overlays stored values onto the built-in defaults, so the raised
  // warmup has to survive a config that never mentions it.
  {
    const v4l2diag::ThresholdConfig config = v4l2diag::default_threshold_config();
    ok &= require(config.get_param("t06-stream-cycles", "rapid_warmup") >= 2.0,
                  "rapid_warmup should resolve to >=2 through ThresholdConfig::get_param");
  }

  if (!ok) {
    return 1;
  }
  std::cout << "pacing tests passed\n";
  return 0;
}
