#include "v4l2diag/core/run_status.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {
bool check(bool condition, const char *what) {
  if (!condition) {
    std::cerr << "FAIL: " << what << "\n";
  }
  return condition;
}
}  // namespace

int main() {
  using v4l2diag::RunOutcome;
  using v4l2diag::resolve_run_outcome;
  using v4l2diag::run_is_truncated;
  using v4l2diag::to_string;
  bool ok = true;

  // A clean run.
  ok &= check(resolve_run_outcome(false, "running", false) == RunOutcome::Completed,
              "an uninterrupted run must be completed");

  // The regression: the worker wrote "completed" over the stop endpoint's
  // "stopped", so a cancelled run looked like a clean one.
  ok &= check(resolve_run_outcome(true, "running", false) == RunOutcome::Stopped,
              "a stop request must survive the worker's final write");
  ok &= check(resolve_run_outcome(false, "stopped", false) == RunOutcome::Stopped,
              "an already-stopped run must not be reset to completed");

  // An exception escaping the worker must be recorded, not kill the process.
  ok &= check(resolve_run_outcome(false, "running", true) == RunOutcome::Error,
              "a failed worker must be recorded as error");
  ok &= check(resolve_run_outcome(true, "stopped", true) == RunOutcome::Error,
              "error must win over stopped");

  // Wire strings the dashboard and history depend on.
  ok &= check(std::string(to_string(RunOutcome::Completed)) == "completed", "completed spelling changed");
  ok &= check(std::string(to_string(RunOutcome::Stopped)) == "stopped", "stopped spelling changed");
  ok &= check(std::string(to_string(RunOutcome::Error)) == "error", "error spelling changed");

  // Only a clean run may feed Pass Rate / Average Duration (plan 2.8).
  ok &= check(!run_is_truncated(RunOutcome::Completed), "a completed run must not be marked truncated");
  ok &= check(run_is_truncated(RunOutcome::Stopped), "a stopped run must be marked truncated");
  ok &= check(run_is_truncated(RunOutcome::Error), "an errored run must be marked truncated");

  // The guard the worker thread actually uses. Nothing may escape it: this
  // runs on a detached thread, where an escaping exception reaches
  // std::terminate and takes the whole server down.
  {
    using v4l2diag::WorkerFailure;
    using v4l2diag::run_guarded;

    WorkerFailure clean = run_guarded([] {});
    ok &= check(!clean.failed, "a successful job was reported as failed");
    ok &= check(clean.message.empty(), "a successful job produced a failure message");

    WorkerFailure std_ex = run_guarded([] { throw std::runtime_error("reqbufs exploded"); });
    ok &= check(std_ex.failed, "a std::exception escaped the guard");
    ok &= check(std_ex.message == "reqbufs exploded", "the exception message was lost");

    WorkerFailure unknown = run_guarded([] { throw 42; });
    ok &= check(unknown.failed, "a non-std exception escaped the guard");
    ok &= check(!unknown.message.empty(), "an unknown exception produced no message");

    // Both failure kinds must land on error + truncated.
    for (const WorkerFailure &f : {std_ex, unknown}) {
      const RunOutcome outcome = resolve_run_outcome(false, "running", f.failed);
      ok &= check(outcome == RunOutcome::Error, "a guarded failure did not resolve to error");
      ok &= check(run_is_truncated(outcome), "a guarded failure was not marked truncated");
    }
  }

  // The recovery path is inside the guard too. A failure finaliser that throws
  // -- while building JSON, appending a log line, or writing the history --
  // must not escape the worker thread either.
  {
    using v4l2diag::GuardedOutcome;
    using v4l2diag::run_guarded_with_recovery;

    // Workload succeeds: recovery must not run at all.
    int recoveries = 0;
    GuardedOutcome clean = run_guarded_with_recovery([] {}, [&](const std::string &) { recoveries++; });
    ok &= check(!clean.work.failed, "a successful workload was reported as failed");
    ok &= check(clean.recovery_calls == 0, "recovery ran for a successful workload");
    ok &= check(recoveries == 0, "recovery body ran for a successful workload");

    // Workload throws: recovery runs exactly once, with the message.
    recoveries = 0;
    std::string seen;
    GuardedOutcome recovered = run_guarded_with_recovery([] { throw std::runtime_error("write_reports blew up"); },
                                                         [&](const std::string &m) {
                                                           recoveries++;
                                                           seen = m;
                                                         });
    ok &= check(recovered.work.failed, "the workload failure was not reported");
    ok &= check(recovered.recovery_calls == 1, "recovery was not called exactly once");
    ok &= check(recoveries == 1, "recovery body did not run exactly once");
    ok &= check(seen == "write_reports blew up", "recovery did not receive the failure message");
    ok &= check(!recovered.recovery.failed, "a clean recovery was reported as failed");

    // Both throw: nothing escapes, recovery still ran exactly once, and both
    // failures are visible to the caller.
    recoveries = 0;
    GuardedOutcome both = run_guarded_with_recovery([] { throw std::runtime_error("runner exploded"); },
                                                    [&](const std::string &) {
                                                      recoveries++;
                                                      throw std::runtime_error("persist failed too");
                                                    });
    ok &= check(both.work.failed && both.work.message == "runner exploded", "the workload failure was lost");
    ok &= check(both.recovery_calls == 1, "recovery ran more or less than once when it threw");
    ok &= check(recoveries == 1, "recovery body ran more or less than once when it threw");
    ok &= check(both.recovery.failed && both.recovery.message == "persist failed too",
                "the recovery failure was not reported");

    // A recovery path that throws something non-std must also be contained.
    GuardedOutcome unknown_recovery =
        run_guarded_with_recovery([] { throw std::runtime_error("x"); }, [](const std::string &) { throw 7; });
    ok &= check(unknown_recovery.recovery.failed, "a non-std recovery exception escaped");
    ok &= check(unknown_recovery.recovery_calls == 1, "recovery call count wrong for a non-std throw");

    // No recovery supplied: still must not escape.
    GuardedOutcome no_recover = run_guarded_with_recovery([] { throw std::runtime_error("y"); }, nullptr);
    ok &= check(no_recover.work.failed, "the workload failure was lost without a recovery callback");
    ok &= check(no_recover.recovery_calls == 0, "recovery was counted without a callback");
  }

  return ok ? 0 : 1;
}
