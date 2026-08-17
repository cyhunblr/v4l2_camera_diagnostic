#pragma once

#include <string>

#include "v4l2diag/core/role_bindings.hpp"
#include "v4l2diag/core/types.hpp"

// Rules that need both a run's TriggerMode and its role routing. Kept apart from
// role_bindings.hpp so that header stays free of types.hpp, which itself needs
// RoleBinding for RunResult.
namespace v4l2diag {

// --- Channel/mode compatibility -------------------------------------------
//
// A hardware channel cannot serve a software-trigger run and vice versa. This
// lived only in the web server, so the CLI did not check it at all; both go
// through here now, which also means they refuse a run with the same words.
//
// `mode` is the run's trigger mode and `channel_is_hardware` says which kind the
// resolved channel is. Free-run routes nothing, so it is vacuously compatible.
bool channel_matches_mode(TriggerMode mode, bool channel_is_hardware);

// The refusal text for an incompatible channel, empty when it is compatible.
std::string describe_mode_mismatch(TriggerMode mode, bool channel_is_hardware, const std::string &role,
                                   const std::string &channel_id);

// --- Free-run consistency --------------------------------------------------
//
// Free-run neither needs nor uses a Trigger Profile, so a run carrying both is
// contradictory. Normalised in ONE place rather than checked at each surface:
// returns the profile id a run should actually use, which is empty for free-run.
// Applied at three entry points -- the CLI (command_run), the JSON parser
// (run_config_from_json) and DiagnosticRunner::run() itself. The runner does not
// rely on its callers having normalised: core is reachable directly, so it does its
// own. None of the three can produce a result that says "free-run" and names a
// profile.
std::string effective_trigger_profile_id(TriggerMode mode, const std::string &requested_profile_id);

}  // namespace v4l2diag
