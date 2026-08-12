// Verdict rules that the hardware-touching test bodies delegate to.
//
// The bodies themselves need a real V4L2 device, so the decision -- "given
// these per-configuration outcomes, what is the status?" -- is factored out
// and checked here. t07 in particular used to end with an unconditional
// TestStatus::Pass, so no failure it recorded could ever change the verdict.
#include "v4l2diag/core/diagnostic_runner.hpp"

#include "v4l2diag/core/test_registry.hpp"

#include <cstddef>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

bool check(bool condition, const char *what) {
  if (!condition) {
    std::cerr << "FAIL: " << what << "\n";
  }
  return condition;
}

std::string read_file(const std::string &path) {
  std::ifstream input(path);
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
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

  // --- t05: STREAMOFF rejection and recovery ------------------------------------
  //
  // Observed: the TestStatus the rule returns for each (dqbuf_failed, restreamon_ok,
  // recovery_ok) combination, against min_recovery_ok = 2.
  //
  // The 2026-08-10 device run (1786329594-21268) recorded dqbuf_failed=1, restreamon_ok=1,
  // recovery_ok=0 and the report said WARN, while the approved preview shows FAIL with the
  // sentence "the restarted stream delivered no recovery frames (0/3)".
  {
    using v4l2diag::pollerr_recovery_verdict;

    // The passing shape: rejection worked, restart worked, recovery met the threshold.
    ok &= check(pollerr_recovery_verdict(true, true, 3, 2) == TestStatus::Pass,
                "full recovery after a correct DQBUF rejection must PASS");
    ok &= check(pollerr_recovery_verdict(true, true, 2, 2) == TestStatus::Pass,
                "recovery exactly at the threshold must PASS");

    // The device run's case: correct rejection, restart reported success, zero frames.
    ok &= check(pollerr_recovery_verdict(true, true, 0, 2) == TestStatus::Fail,
                "zero recovery frames must FAIL -- no recovery is not partial recovery");

    // Genuinely partial, which is the band the test's documentation describes.
    ok &= check(pollerr_recovery_verdict(true, true, 1, 2) == TestStatus::Warn,
                "one frame short of the threshold must WARN");

    // The state-machine violation: a frame came back after STREAMOFF. This is the free-run
    // preview's failure sentence and outranks the recovery outcome -- even a full recovery
    // cannot excuse the driver handing back a frame while streaming is off.
    ok &= check(pollerr_recovery_verdict(false, true, 3, 2) == TestStatus::Fail,
                "DQBUF succeeding after STREAMOFF must FAIL regardless of recovery");

    // Re-STREAMON itself failing is a stalled pipeline, not a partial one.
    ok &= check(pollerr_recovery_verdict(true, false, 0, 2) == TestStatus::Fail, "a failed re-STREAMON must FAIL");

    // A threshold of zero must not turn the zero case into a PASS by accident: the run still
    // delivered nothing, and a configuration cannot define the stall away.
    ok &= check(pollerr_recovery_verdict(true, true, 0, 0) == TestStatus::Fail,
                "zero recovery frames must FAIL even when the threshold is zero");
  }

  // --- the t05 body must DELEGATE to that rule ----------------------------------
  //
  // The checks above only prove the rule is self-consistent. `run_pollerr_handling` needs a
  // real V4L2 device, so it cannot be called here -- and while nothing observed the call site,
  // reverting the body to its own inline `else -> Warn` left every test green. That is a
  // vacuous guard: the rule was right and unused. Observed here: the assignment in the runner
  // source, and the absence of a hand-rolled status ladder beside it.
  {
    const std::string runner = read_file("source/backend/core/src/diagnostic_runner.cpp");
    ok &= check(!runner.empty(), "diagnostic_runner.cpp not readable; run from the repository root");
    const std::size_t body = runner.find("void run_pollerr_handling");
    ok &= check(body != std::string::npos, "run_pollerr_handling not found; this guard would pass vacuously");
    if (body != std::string::npos) {
      // The function ends at the next top-level definition; "// Docs:" precedes each one.
      const std::size_t end = runner.find("// Docs:", body);
      const std::string t05 = runner.substr(body, end == std::string::npos ? std::string::npos : end - body);
      const std::size_t at = t05.find("r.status = pollerr_recovery_verdict(");
      ok &= check(at != std::string::npos,
                  "run_pollerr_handling does not assign its status from pollerr_recovery_verdict()");
      // Only the VERDICT region is scanned -- from the threshold read to the end of the body.
      // The early `return` for a failed session setup legitimately sets Fail, and that is not a
      // verdict about the driver's STREAMOFF behaviour.
      const std::size_t verdict_from = t05.find("min_recovery_frames");
      const std::string tail = verdict_from == std::string::npos ? t05 : t05.substr(verdict_from);
      ok &= check(tail.find("r.status = TestStatus::Warn") == std::string::npos,
                  "run_pollerr_handling still sets a WARN status directly instead of delegating");
      ok &= check(tail.find("r.status = TestStatus::Pass") == std::string::npos &&
                      tail.find("r.status = TestStatus::Fail") == std::string::npos,
                  "run_pollerr_handling still decides PASS/FAIL inline instead of delegating");
    }
  }

  // --- t08 must record a slot line for EVERY retained buffer --------------------
  //
  // The approved t08 card shows one slot per retained buffer, READY ones beside the flagged
  // one, because "1 of 2 carried the flag" is only legible when both are drawn. The runner's
  // loop is hardware-bound, and every fixture hand-writes its own `slot:` lines -- so moving
  // the emission inside the `if (buf.flags & V4L2_BUF_FLAG_ERROR)` branch left the whole suite
  // green while a real run would render only error slots. Observed here: the source position of
  // the emission relative to that branch.
  {
    const std::string runner = read_file("source/backend/core/src/diagnostic_runner.cpp");
    const std::size_t body = runner.find("void run_buffer_overwrite");
    ok &= check(body != std::string::npos, "run_buffer_overwrite not found; this guard would pass vacuously");
    if (body != std::string::npos) {
      const std::size_t end = runner.find("// Docs:", body);
      const std::string t08 = runner.substr(body, end == std::string::npos ? std::string::npos : end - body);
      const std::size_t push = t08.find("slot_details.push_back(");
      ok &= check(push != std::string::npos, "t08 records no per-buffer slot line");
      if (push != std::string::npos) {
        // The push must NOT be guarded by the error-flag test: the line before it decides
        // whether READY buffers reach the report at all.
        const std::string statement = t08.substr(push > 200 ? push - 200 : 0, push - (push > 200 ? push - 200 : 0));
        ok &= check(statement.find("V4L2_BUF_FLAG_ERROR") == std::string::npos,
                    "t08's slot emission is gated on the error flag, so READY buffers never reach the report");
      }
    }
  }

  // --- run_test() must RECORD the settings it resolves --------------------------
  //
  // record_run_parameters() is checked against the approved previews in
  // tests/test_configuration_contract_test.cpp, but that only proves the function is right. If
  // run_test() never calls it, every device report renders an empty Test Configuration and the
  // whole suite still passes -- the same vacuous shape that let the t05 verdict and the t08 slot
  // emission stay unused. Observed here: the call in the runner source, and its position relative
  // to the `tp` map it must read.
  {
    const std::string runner = read_file("source/backend/core/src/diagnostic_runner.cpp");
    ok &= check(!runner.empty(), "diagnostic_runner.cpp not readable; run from the repository root");
    const std::size_t body = runner.find("TestResult DiagnosticRunner::run_test");
    ok &= check(body != std::string::npos, "run_test not found; this guard would pass vacuously");
    if (body != std::string::npos) {
      const std::string tail = runner.substr(body);
      const std::size_t call = tail.find("record_run_parameters(&result");
      ok &= check(call != std::string::npos, "run_test() never records the settings it resolved");
      // The call must be UNCONDITIONAL. `if (false) record_run_parameters(...)` leaves the text in
      // place, so a check for the call alone passes while no report gets a single row -- measured:
      // that exact sabotage passed the first version of this guard. The 60 characters before the
      // call must not open a conditional.
      if (call != std::string::npos) {
        const std::string before = tail.substr(call > 60 ? call - 60 : 0, call - (call > 60 ? call - 60 : 0));
        ok &= check(before.find("if (") == std::string::npos && before.find("if(") == std::string::npos &&
                        before.find("? ") == std::string::npos,
                    "run_test()'s settings recording is behind a condition; a guarded-out call records nothing");
      }
      // It must come AFTER the injected profile values are in `tp`, or the recorded pulse width is
      // whatever the parameter table held rather than what the run fires.
      const std::size_t inject = tail.find("tp[\"__pulse_width_ns\"]");
      if (call != std::string::npos && inject != std::string::npos) {
        ok &= check(inject < call,
                    "run_test() records its settings before injecting the profile values into tp; "
                    "the recorded pulse width would not be the one the run fires");
      }
    }
  }

  return ok ? 0 : 1;
}
