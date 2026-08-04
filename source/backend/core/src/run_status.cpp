#include "v4l2diag/core/run_status.hpp"

#include <exception>
#include <functional>
#include <string>

namespace v4l2diag {

const char *to_string(RunOutcome outcome) {
  switch (outcome) {
    case RunOutcome::Stopped:
      return "stopped";
    case RunOutcome::Error:
      return "error";
    case RunOutcome::Completed:
    default:
      return "completed";
  }
}

WorkerFailure run_guarded(const std::function<void()> &work) {
  WorkerFailure failure;
  try {
    if (work) {
      work();
    }
  } catch (const std::exception &e) {
    failure.failed = true;
    failure.message = e.what();
  } catch (...) {
    failure.failed = true;
    failure.message = "unknown error";
  }
  return failure;
}

GuardedOutcome run_guarded_with_recovery(const std::function<void()> &work,
                                         const std::function<void(const std::string &)> &recover) {
  GuardedOutcome outcome;
  outcome.work = run_guarded(work);
  if (!outcome.work.failed || !recover) {
    return outcome;
  }
  outcome.recovery_calls = 1;
  const std::string message = outcome.work.message;
  outcome.recovery = run_guarded([&recover, &message]() { recover(message); });
  return outcome;
}

RunOutcome resolve_run_outcome(bool stop_requested, const std::string &current_status, bool worker_failed) {
  if (worker_failed) {
    return RunOutcome::Error;
  }
  if (stop_requested || current_status == "stopped") {
    return RunOutcome::Stopped;
  }
  return RunOutcome::Completed;
}

bool run_is_truncated(RunOutcome outcome) {
  return outcome != RunOutcome::Completed;
}

}  // namespace v4l2diag
