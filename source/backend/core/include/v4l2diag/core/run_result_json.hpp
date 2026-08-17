#pragma once

#include <json/json.h>

#include <string>

#include "v4l2diag/core/types.hpp"

namespace v4l2diag {

// The canonical structured result (plan 2.6.2).
//
// ONE serializer for both surfaces. The live API and the JSON report artifact used to
// be written by two hand-maintained copies -- one building a Json::Value, one
// streaming text -- and they had already drifted: `project_name` vs `project`,
// `camera_path` vs `path`, and empty arrays that one wrote and the other omitted.
//
// That mattered once the API started reading the artifact back after a restart: two
// shapes for the same fact means the restored result is not the result that was
// served live. So both go through here, and the API only wraps it in
// {id, status, result}.
constexpr int kResultSchemaVersion = 1;

// Serialises a run result. The document always carries `result_schema_version`, so a
// reader can tell what shape it is looking at instead of guessing.
Json::Value run_result_to_json(const RunResult &result);

// The inverse, for reading an artifact back. Returns false when the document cannot
// be trusted; `*error` says why.
//
// Refuses rather than guesses:
//   * a version newer than this build understands -> refused, not parsed as current
//   * a malformed document -> refused
//   * fields absent from an older artifact are left at their defaults and NOT
//     invented (trigger_profile_id, role, role_bindings, timing)
//
// A limited legacy adapter maps field names earlier builds wrote (`project` ->
// `project_name`, `cameras[].path` -> `camera_path`).
bool run_result_from_json(const Json::Value &root, RunResult *result, std::string *error);

}  // namespace v4l2diag
