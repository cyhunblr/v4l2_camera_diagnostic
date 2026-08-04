#pragma once

#include <json/json.h>

#include <string>

#include "v4l2diag/core/config_version.hpp"
#include "v4l2diag/core/profile_registry.hpp"

namespace v4l2diag {

// Classifies a parsed JSON document. `*version` receives the version read from
// the document (0 when absent or unusable).
ConfigVersionState classify_profile_version(const Json::Value &root, int *version);

// The single migration entry point.
//
// Both the disk loader (ProfileRegistry::load) and the API/import path must call
// this so a profile is interpreted identically however it arrives; they used to
// parse the same JSON in two hand-written copies.
//
// Returns true when *profile was populated (Current or Legacy). Never writes to
// disk: migration is in-memory, and the stored file is only rewritten when the
// user explicitly saves.
bool migrate_profile_json(const Json::Value &root, DeviceProfile *profile, MigrationReport *report);

// Convenience wrapper that reads and parses the file first. A file that cannot
// be read or parsed yields ConfigVersionState::Malformed.
bool migrate_profile_file(const std::string &path, DeviceProfile *profile, MigrationReport *report);

// Serialises a report for the wire, so the UI can render migration state without
// re-deriving it. Shape:
//   {"state": "...", "source_version": N, "target_version": N,
//    "migrated": [...], "dropped": [...], "required": [...],
//    "usable_for_run": bool, "needs_user_input": bool, "error": "..."}
Json::Value migration_report_to_json(const MigrationReport &report);

// Classifies a runs-index.json document. A bare array is what older builds
// wrote: Legacy, readable, and it gains the version on the next write.
RunsIndexState classify_runs_index(const Json::Value &root);

}  // namespace v4l2diag
