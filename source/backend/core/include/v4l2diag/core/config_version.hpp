#pragma once

#include <string>
#include <vector>

namespace v4l2diag {

// Schema version this build writes and understands.
//
// v3 dropped `enabled`: a profile is either present or deleted, and the extra
// flag let a profile exist but be invisible with no way to tell from the file
// which state was intended.
//
// v4 drops `camera_match` and matcher-based `camera_bindings`, and adds
// `role_bindings`. A Trigger Profile no longer identifies a physical camera:
// {driver, card, bus_info} cannot tell this hardware's four video nodes apart,
// so routing goes through run roles. The old physical bindings are NOT converted
// automatically -- see migrate_profile_json().
//
// v5 drops `defaults.report_formats`. The user does not choose formats any more:
// every run writes HTML, JSON and Markdown. Unlike every other schema step this one
// is LOSSLESS and needs no user input, so it gets one narrow exception -- see
// `lossless_upgrade` below.
constexpr int kProfileSchemaVersion = 5;

// runs-index.json had no version at all, so the reader could not tell the shape
// it was looking at. See RunsIndexState below.
constexpr int kRunsIndexSchemaVersion = 1;

// How a stored config relates to this build.
enum class ConfigVersionState {
  Current,    // exactly kProfileSchemaVersion — load as is
  Legacy,     // older, and this build knows how to migrate it
  Future,     // newer — refuse rather than guess at fields we do not know
  Malformed,  // unparseable, not an object, or no usable version field
};

const char *to_string(ConfigVersionState state);

// Outcome of reading a stored config, with the three kinds of change reported
// separately so the UI can say what happened rather than "something changed".
struct MigrationReport {
  ConfigVersionState state = ConfigVersionState::Malformed;
  int source_version = 0;
  int target_version = kProfileSchemaVersion;

  // Fields carried across, as "old -> new" or a plain field name when the value
  // moved unchanged but its meaning is version-dependent.
  std::vector<std::string> migrated;
  // Fields the target schema no longer has. Their values are discarded.
  std::vector<std::string> dropped;
  // Mandatory fields the stored config cannot supply. The user must fill these
  // in; until then the config must not be used for a run.
  std::vector<std::string> required;

  // Field-level validation failures (duplicate channel id, negative GPIO line,
  // software channel with no fire control, dangling binding, out-of-range
  // trigger timing, unrecognised enum value). These block a run just as surely
  // as a missing mandatory field, and are reported separately so the UI can
  // point at the field rather than show one opaque error.
  std::vector<std::string> invalid;

  // Set for Future and Malformed, empty otherwise.
  std::string error;

  // True when parsing got far enough to produce values worth showing. Set by the
  // migration entry point, not derived from `state`: a Current config that fails
  // field validation also has values to prefill, and deriving this from Legacy
  // alone left the migration form empty for exactly the configs that need it.
  bool draft_available = false;

  // A Legacy config whose every difference from the current schema is lossless and
  // needs no user input (plan 2.10.2). Only the v4 -> v5 `report_formats` removal
  // qualifies today: nothing is asked of the user, so the profile is not taken away
  // from them either.
  //
  // Deliberately NOT a general loosening of "Legacy is not runnable". It is set only
  // when the migration found no other difference, so `enabled: false`, a missing
  // `role_bindings`, Future and Malformed cannot reach it.
  bool lossless_upgrade = false;

  // A run may only use a config stored at the current schema version.
  //
  // A Legacy config is migrated in memory so its values can be shown, but it is
  // NOT runnable: the user has to review the migration and save it explicitly.
  // Auto-promoting one would silently decide things the stored file never said
  // -- a v2 profile with `enabled: false` would come back active.
  bool usable_for_run() const {
    if (!required.empty() || !invalid.empty()) {
      return false;
    }
    return state == ConfigVersionState::Current || (state == ConfigVersionState::Legacy && lossless_upgrade);
  }

  // True when the user has to act before this config can be selected: either the
  // schema moved, a mandatory field is missing, or a field is invalid.
  bool needs_user_input() const {
    if (!required.empty() || !invalid.empty()) {
      return true;
    }
    // A lossless upgrade has nothing to ask about.
    return state == ConfigVersionState::Legacy && !lossless_upgrade;
  }

  bool changed() const {
    return !migrated.empty() || !dropped.empty();
  }

  // Whether a draft accompanies this report. Single source of truth with
  // `draft_available` so the wire flag and the presence of `draft_profile` can
  // never disagree.
  bool has_draft() const {
    return draft_available;
  }
};

// --- runs-index.json ------------------------------------------------------
//
// Written without a version, so a reader could not tell whether it was looking
// at the current shape. An index with no version is treated as Legacy: it is
// readable, and gains the version the next time it is written.
struct RunsIndexState {
  ConfigVersionState state = ConfigVersionState::Malformed;
  int source_version = 0;
  int target_version = kRunsIndexSchemaVersion;
};

}  // namespace v4l2diag
