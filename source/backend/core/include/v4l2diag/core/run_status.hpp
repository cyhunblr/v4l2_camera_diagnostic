#pragma once

#include <functional>
#include <string>

namespace v4l2diag {

// How a diagnostic run ended, as recorded in the run history.
//
// The worker used to write "completed" unconditionally, which overwrote the
// "stopped" the stop endpoint had already set: a cancelled run was
// indistinguishable from a clean one, and its partial counters then skewed the
// dashboard's aggregates. An exception escaping the worker was worse -- it
// terminated the whole server rather than being recorded as "error".
enum class RunOutcome { Completed, Stopped, Error };

// Outcome of running work behind the worker-thread guard.
struct WorkerFailure {
  bool failed = false;
  std::string message;
};

// Runs `work` and lets nothing escape -- not std::exception, not anything else.
// This is the guard the run worker thread uses at its entry point, so any throw
// from setup, the run itself, report writing, status finalisation or logging is
// turned into data instead of a std::terminate that kills the server.
WorkerFailure run_guarded(const std::function<void()> &work);

// Result of running work plus its failure-recovery path behind one guard.
struct GuardedOutcome {
  WorkerFailure work;      // did the workload fail
  WorkerFailure recovery;  // did the recovery path itself fail
  int recovery_calls = 0;  // 0 when the workload succeeded, otherwise 1
};

// Runs `work`; if it throws, runs `recover(message)` exactly once. Neither the
// workload nor the recovery path may escape -- this is the outermost boundary
// of the run worker thread, so a throw from the recovery path (building the
// history JSON, appending a log line, writing runs-index.json) would otherwise
// reach std::terminate just as surely as one from the run itself.
GuardedOutcome run_guarded_with_recovery(const std::function<void()> &work,
                                         const std::function<void(const std::string &)> &recover);

const char *to_string(RunOutcome outcome);

// Resolves the status the worker should persist.
//
//   worker_failed  -> Error    (an exception escaped the run or report writing)
//   stop requested -> Stopped  (either the flag or an already-set "stopped")
//   otherwise      -> Completed
//
// Error wins over Stopped: a run that was cancelled and then threw is still a
// failure the user needs to see.
RunOutcome resolve_run_outcome(bool stop_requested, const std::string &current_status, bool worker_failed);

// A run whose test list was cut short carries partial per-test counters, so it
// must not feed the dashboard's Pass Rate or Average Duration (see plan 2.8).
bool run_is_truncated(RunOutcome outcome);

}  // namespace v4l2diag
