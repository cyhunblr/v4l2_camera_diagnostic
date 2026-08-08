// Verdict rules that the hardware-touching test bodies delegate to.
//
// The bodies themselves need a real V4L2 device, so the decision -- "given
// these per-configuration outcomes, what is the status?" -- is factored out
// and checked here. t07 in particular used to end with an unconditional
// TestStatus::Pass, so no failure it recorded could ever change the verdict.
#include "v4l2diag/core/diagnostic_runner.hpp"

#include "v4l2diag/core/test_registry.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace {

bool check(bool condition, const char *what) {
  if (!condition) {
    std::cerr << "FAIL: " << what << "\n";
  }
  return condition;
}

v4l2diag::MultiBufferOutcome ok_cfg(int requested, int samples) {
  v4l2diag::MultiBufferOutcome o;
  o.requested = requested;
  o.setup_ok = true;
  o.samples_requested = samples;
  o.samples_captured = samples;
  return o;
}

v4l2diag::MultiBufferOutcome broken_cfg(int requested) {
  v4l2diag::MultiBufferOutcome o;
  o.requested = requested;
  o.setup_ok = false;
  return o;
}

}  // namespace

int main() {
  using v4l2diag::multi_buffer_verdict;
  using v4l2diag::MultiBufferOutcome;
  using v4l2diag::TestStatus;
  bool ok = true;

  // Every configuration allocated, started and captured everything it asked for.
  ok &= check(multi_buffer_verdict({ok_cfg(1, 20), ok_cfg(2, 20), ok_cfg(3, 20)}) == TestStatus::Pass,
              "a fully successful sweep must PASS");

  // No configuration got as far as a usable session.
  ok &= check(multi_buffer_verdict({broken_cfg(1), broken_cfg(2)}) == TestStatus::Fail,
              "a sweep where nothing worked must FAIL");
  ok &= check(multi_buffer_verdict({}) == TestStatus::Fail, "an empty sweep must FAIL");

  // Some configurations failed but at least one is usable.
  ok &= check(multi_buffer_verdict({broken_cfg(1), ok_cfg(2, 20)}) == TestStatus::Warn,
              "a partially broken sweep must WARN");

  // Setup succeeded everywhere but frames were missed.
  {
    MultiBufferOutcome missed = ok_cfg(2, 20);
    missed.samples_captured = 18;
    ok &= check(multi_buffer_verdict({ok_cfg(1, 20), missed}) == TestStatus::Warn, "a capture miss must WARN");
  }

  // Requested != allocated is normal driver behaviour and must not, on its own,
  // move the verdict. This is the case the free-run reference preview shows:
  // five requests all collapse to an effective depth of 2.
  {
    MultiBufferOutcome clamped = ok_cfg(5, 20);
    clamped.allocated = 2;
    ok &= check(multi_buffer_verdict({clamped}) == TestStatus::Pass,
                "a requested/allocated mismatch alone must not change the verdict");
  }

  // Every configuration allocated and started, but not one delivered a frame.
  // §5.7.3 rule 1: nothing completed the allocation/start/capture chain, so
  // this is FAIL. It used to WARN because setup alone counted as "usable".
  {
    MultiBufferOutcome a = ok_cfg(1, 20);
    MultiBufferOutcome b = ok_cfg(2, 20);
    a.samples_captured = 0;
    b.samples_captured = 0;
    ok &= check(multi_buffer_verdict({a, b}) == TestStatus::Fail,
                "a sweep where no configuration captured a frame must FAIL");
  }

  // One configuration did deliver frames: the other's total miss is then a
  // partial result, not a dead test.
  {
    MultiBufferOutcome dead = ok_cfg(1, 20);
    dead.samples_captured = 0;
    ok &= check(multi_buffer_verdict({dead, ok_cfg(2, 20)}) == TestStatus::Warn,
                "a total miss alongside a working configuration must WARN");
  }

  // Allocation-only sweep (capture never attempted). This must not read as a
  // pass: no configuration completed the capture stage. In production it is
  // unreachable -- t07 declares uses_trigger, so run_test() returns SKIP with
  // the trigger reason before the sweep runs -- and free-run supplies a
  // FreeRunTrigger, so capture is always attempted there.
  {
    MultiBufferOutcome probe_only = ok_cfg(3, 0);
    ok &= check(multi_buffer_verdict({probe_only}) == TestStatus::Fail, "an allocation-only probe must not PASS");
  }

  // The SKIP above depends on t07 declaring that it uses a trigger. Lock that,
  // because clearing the flag would silently route a trigger-less run into the
  // allocation-only path instead of skipping it.
  {
    bool found = false;
    for (const auto &test : v4l2diag::built_in_tests()) {
      if (test.id == "t07-multi-buffer") {
        found = true;
        ok &= check(test.uses_trigger, "t07 must declare uses_trigger so a missing trigger yields SKIP");
      }
    }
    ok &= check(found, "t07-multi-buffer is missing from the registry");
  }

  // --- t16 pulse width sweep (report-ui-design-spec.md §5.16.8) -----------
  {
    using v4l2diag::pulse_width_verdict;
    using v4l2diag::PulseWidthOutcome;

    auto width = [](int ms, int requested, int captured) {
      PulseWidthOutcome o;
      o.width_ms = ms;
      o.samples_requested = requested;
      o.samples_captured = captured;
      return o;
    };

    ok &= check(pulse_width_verdict({width(1, 8, 8), width(30, 8, 8)}, true) == TestStatus::Pass,
                "a complete sweep with conclusive edge evidence must PASS");
    ok &= check(pulse_width_verdict({width(1, 8, 8), width(30, 8, 8)}, false) == TestStatus::Warn,
                "inconclusive edge evidence must WARN");
    ok &= check(pulse_width_verdict({width(1, 8, 3), width(30, 8, 8)}, true) == TestStatus::Warn,
                "a partial hit at one width must WARN");
    ok &= check(pulse_width_verdict({width(1, 8, 0), width(30, 8, 0)}, true) == TestStatus::Fail,
                "a sweep that captured nothing must FAIL");
    ok &= check(pulse_width_verdict({}, true) == TestStatus::Fail, "an empty sweep must FAIL");
  }

  return ok ? 0 : 1;
}
