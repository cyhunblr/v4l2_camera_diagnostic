#include "v4l2diag/core/config_migration.hpp"

#include "v4l2diag/core/config_version.hpp"
#include "v4l2diag/core/role_bindings.hpp"

#include <json/json.h>

#include <algorithm>
#include <set>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "v4l2diag/core/types.hpp"

namespace v4l2diag {

namespace {

bool valid_id(const std::string &id) {
  if (id.empty()) {
    return false;
  }
  return std::all_of(id.begin(), id.end(), [](unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
  });
}

// Returns false for anything unrecognised. The kind decides which device the
// control writes are aimed at, so falling back to CaptureDevice would send a
// software trigger's writes to the wrong node -- the same reason trigger_mode and
// channel type are not guessed at either.
bool selector_kind_from_string(const std::string &value, ControlDeviceSelector::Kind *kind) {
  if (value == "capture") {
    *kind = ControlDeviceSelector::Kind::CaptureDevice;
    return true;
  }
  if (value == "video") {
    *kind = ControlDeviceSelector::Kind::VideoDevice;
    return true;
  }
  if (value == "subdevice") {
    *kind = ControlDeviceSelector::Kind::SubDevice;
    return true;
  }
  return false;
}

V4l2ControlWrite control_write_from_json(const Json::Value &root) {
  V4l2ControlWrite write;
  write.id = root.get("id", 0).asUInt();
  write.name = root.get("name", "").asString();
  write.type = root.get("type", 0).asUInt();
  write.value = root.get("value", 0).asInt64();
  return write;
}

// Reads the version field without interpreting anything else. A version must be
// a positive integer; "two", 0 and -1 are malformed rather than defaulted --
// guessing here is how an unreadable file becomes a silently wrong one.
bool read_version(const Json::Value &root, int *version) {
  if (!root.isObject() || !root.isMember("schema_version")) {
    return false;
  }
  const Json::Value &value = root["schema_version"];
  if (!value.isIntegral()) {
    return false;
  }
  const int parsed = value.asInt();
  if (parsed <= 0) {
    return false;
  }
  *version = parsed;
  return true;
}

// Fields the target schema no longer has. Read only so the report can say they
// were discarded.
void collect_dropped(const Json::Value &root, int source_version, MigrationReport *report) {
  if (source_version <= 3) {
    // v4 removed physical camera matching. The values are reported rather than
    // converted: there is no reliable mapping from a physical camera to a run
    // role, and firing the wrong camera is worse than making the user fill in a
    // field. See plan 2.5.2.
    if (root.isMember("camera_match")) {
      const Json::Value &matcher = root["camera_match"];
      std::string described;
      for (const char *field : {"driver", "card", "bus_info"}) {
        const std::string value = matcher.get(field, "").asString();
        if (!value.empty()) {
          if (!described.empty()) {
            described += ", ";
          }
          described += std::string(field) + "=" + value;
        }
      }
      report->dropped.push_back("camera_match" + (described.empty() ? std::string() : " {" + described + "}") +
                                " (v4 routes by run role, not by physical camera)");
    }
    if (root.isMember("camera_bindings") && !root["camera_bindings"].empty()) {
      report->dropped.push_back("camera_bindings (" + std::to_string(root["camera_bindings"].size()) +
                                " physical binding(s); v4 needs role_bindings, which cannot be derived from these)");
    }
  }
  // Any version below the one that removed it (v3). Was `<= 2`, which silently
  // ignored the key in a hand-edited v4 file -- and that silence made such a file
  // look like a lossless v4 -> v5 upgrade, so it stayed runnable with a flag nobody
  // reported. Reported for every pre-current version instead.
  if (source_version < kProfileSchemaVersion && root.isMember("enabled")) {
    // The old value is part of the report on purpose. A profile stored with
    // `enabled: false` was deliberately hidden, and the user has to see that
    // before saving it at the current schema -- the two cases must not read the
    // same on the migration screen.
    const Json::Value &value = root["enabled"];
    const std::string was = value.isBool() ? (value.asBool() ? "true" : "false") : "not a boolean";
    report->dropped.push_back("enabled=" + was +
                              " (v3 removed the flag: a profile is either present or deleted"
                              ", so saving this config makes it selectable)");
  }
}

// Mandatory fields the stored config cannot supply. Reported, never invented.
void collect_required(const DeviceProfile &profile, MigrationReport *report) {
  if (!valid_id(profile.id)) {
    report->required.push_back("id");
  }
  if (profile.name.empty()) {
    report->required.push_back("name");
  }
  if (profile.trigger_channels.empty()) {
    report->required.push_back("trigger_channels (at least one channel)");
  }
  // A triggered profile has to say which role fires on which channel. Free-run
  // does not route at all, so it legitimately has none. Never inferred -- not even
  // when there is exactly one channel to pick (plan 2.5.2).
  if (profile.defaults.trigger_mode != TriggerMode::FreeRun) {
    const bool binds_master = std::any_of(profile.role_bindings.begin(), profile.role_bindings.end(),
                                          [](const RoleBinding &b) { return b.role == kMasterRole(); });
    if (!binds_master) {
      // Specifically master: every run has one, so a slave-only profile is as
      // unusable as an empty one.
      report->required.push_back("role_bindings (must bind master; v4 routes by run role)");
    }
  }
}

// Field-level checks. These mirror validate_device_profile()'s rules, but report
// per field so the UI can point at the offender instead of showing one opaque
// message -- and so a stored config that fails them can never reach a run.
void collect_invalid(const DeviceProfile &profile, MigrationReport *report) {
  std::set<std::string> channel_ids;
  for (const auto &channel : profile.trigger_channels) {
    if (!valid_id(channel.id)) {
      report->invalid.push_back("trigger_channels[" + channel.id + "].id is not a valid identifier");
    } else if (!channel_ids.insert(channel.id).second) {
      report->invalid.push_back("trigger_channels[" + channel.id + "].id is duplicated");
    }
    if (channel.type == TriggerChannel::Type::Hardware && channel.gpio.line_number < 0) {
      report->invalid.push_back("trigger_channels[" + channel.id + "].gpio.line_number cannot be negative");
    }
    if (channel.type == TriggerChannel::Type::Software && channel.fire_controls.empty()) {
      report->invalid.push_back("trigger_channels[" + channel.id + "].fire requires at least one control");
    }
  }
  // role_bindings: channel must exist, role must be one of the canonical names,
  // and a role may not repeat. The run-topology check (does this binding set match
  // THIS run's camera count) happens per run in resolve_role_bindings(); here we
  // only reject what is wrong regardless of topology.
  std::set<std::string> bound_roles;
  for (const auto &binding : profile.role_bindings) {
    if (channel_ids.count(binding.trigger_channel_id) == 0) {
      report->invalid.push_back("role_bindings[" + binding.role + "] references unknown trigger channel \"" +
                                binding.trigger_channel_id + "\"");
    }
    if (!bound_roles.insert(binding.role).second) {
      report->invalid.push_back("role_bindings binds role \"" + binding.role + "\" more than once");
    }
    if (!is_canonical_role(binding.role)) {
      report->invalid.push_back("role_bindings[" + binding.role +
                                "] is not a canonical role (master, slave-1, slave-2, ...)");
    }
  }
  if (profile.defaults.trigger_rate_hz <= 0 || profile.defaults.trigger_rate_hz > 1000) {
    report->invalid.push_back("defaults.trigger_rate_hz must be > 0 and <= 1000");
  }
  if (profile.defaults.pulse_width_ms <= 0 || profile.defaults.pulse_width_ms > 100) {
    report->invalid.push_back("defaults.pulse_width_ms must be > 0 and <= 100");
  }
}

}  // namespace

const char *to_string(ConfigVersionState state) {
  switch (state) {
    case ConfigVersionState::Current:
      return "current";
    case ConfigVersionState::Legacy:
      return "legacy";
    case ConfigVersionState::Future:
      return "future";
    case ConfigVersionState::Malformed:
      return "malformed";
  }
  return "malformed";
}

ConfigVersionState classify_profile_version(const Json::Value &root, int *version) {
  int parsed = 0;
  if (!read_version(root, &parsed)) {
    if (version != nullptr) {
      *version = 0;
    }
    return ConfigVersionState::Malformed;
  }
  if (version != nullptr) {
    *version = parsed;
  }
  if (parsed == kProfileSchemaVersion) {
    return ConfigVersionState::Current;
  }
  return parsed < kProfileSchemaVersion ? ConfigVersionState::Legacy : ConfigVersionState::Future;
}

bool migrate_profile_json(const Json::Value &root, DeviceProfile *profile, MigrationReport *report) {
  MigrationReport local;
  MigrationReport &out = report != nullptr ? *report : local;
  out = MigrationReport();
  out.target_version = kProfileSchemaVersion;
  out.state = classify_profile_version(root, &out.source_version);

  if (out.state == ConfigVersionState::Malformed) {
    out.error = "config is not a versioned JSON object";
    return false;
  }
  if (out.state == ConfigVersionState::Future) {
    out.error = "config schema version " + std::to_string(out.source_version) + " is newer than this build supports (" +
                std::to_string(kProfileSchemaVersion) + ")";
    return false;
  }

  DeviceProfile parsed;
  parsed.schema_version = root.get("schema_version", 2).asInt();
  parsed.id = root.get("id", "").asString();
  // Not defaulted to the id: a missing name is reported as required so the user
  // supplies one, rather than silently becoming "bench-rig" for profile "bench-rig".
  parsed.name = root.get("name", "").asString();
  parsed.description = root.get("description", "").asString();

  const Json::Value &defaults = root["defaults"];
  if (defaults.isMember("trigger_mode")) {
    const std::string mode = defaults["trigger_mode"].asString();
    if (!parse_trigger_mode(mode, &parsed.defaults.trigger_mode)) {
      // Not silently free-run: an unreadable mode is a field the user must fix.
      out.invalid.push_back("defaults.trigger_mode: \"" + mode + "\" is not a trigger mode");
    }
  }
  for (const auto &value : defaults["memory_backends"]) {
    MemoryBackend backend;
    const std::string name = value.asString();
    if (parse_memory_backend(name, &backend)) {
      parsed.defaults.memory_backends.push_back(backend);
    } else if (!name.empty()) {
      out.invalid.push_back("defaults.memory_backends: \"" + name + "\" is not a memory backend");
    }
  }
  for (const auto &value : defaults["test_selectors"]) {
    parsed.defaults.test_selectors.push_back(value.asString());
  }
  // defaults.report_formats is NOT read: v5 removed it (plan 2.10). Every run
  // writes HTML, JSON and Markdown, so a stored preference has nothing to select.
  // It is reported as dropped below, with its old value.
  parsed.defaults.trigger_rate_hz = defaults.get("trigger_rate_hz", 30.0).asDouble();
  parsed.defaults.pulse_width_ms = defaults.get("pulse_width_ms", 13.0).asDouble();

  for (const auto &value : root["trigger_channels"]) {
    TriggerChannel channel;
    channel.id = value.get("id", "").asString();
    channel.name = value.get("name", "").asString();
    channel.description = value.get("description", "").asString();
    const std::string channel_type = value.get("type", "").asString();
    if (channel_type == "software") {
      channel.type = TriggerChannel::Type::Software;
    } else if (channel_type == "hardware") {
      channel.type = TriggerChannel::Type::Hardware;
    } else {
      // Not guessed at: which kind a channel is decides what the rest of its
      // fields mean, so an unreadable type is a field the user must fix.
      out.invalid.push_back("trigger_channels[" + channel.id + "].type: \"" + channel_type +
                            "\" is not a channel type");
      channel.type = TriggerChannel::Type::Hardware;
    }
    if (channel.type == TriggerChannel::Type::Hardware) {
      channel.gpio.chip_id = value["gpio"].get("chip_id", 0).asInt();
      channel.gpio.line_number = value["gpio"].get("line_number", 0).asInt();
      channel.gpio.description = value["gpio"].get("description", "").asString();
    } else {
      const Json::Value &selector = value["control_device"];
      const std::string kind = selector.get("kind", "capture").asString();
      if (!selector_kind_from_string(kind, &channel.control_device.kind)) {
        out.invalid.push_back("trigger_channels[" + channel.id + "].control_device.kind: \"" + kind +
                              "\" is not a control device kind");
      }
      channel.control_device.driver = selector.get("driver", "").asString();
      channel.control_device.card = selector.get("card", "").asString();
      channel.control_device.bus_info = selector.get("bus_info", "").asString();
      channel.control_device.sysfs_name = selector.get("sysfs_name", "").asString();
      for (const auto &write : value["setup"]) {
        channel.setup_controls.push_back(control_write_from_json(write));
      }
      for (const auto &write : value["fire"]) {
        channel.fire_controls.push_back(control_write_from_json(write));
      }
      for (const auto &write : value["teardown"]) {
        channel.teardown_controls.push_back(control_write_from_json(write));
      }
    }
    parsed.trigger_channels.push_back(channel);
  }

  // role_bindings is a v4 field, so it is read only from v4 onwards. A v3 file
  // that happens to carry the key was not written by any released build: trusting
  // it would let a hand-edited legacy file skip the migration form, which is the
  // review step plan 2.5.2 exists for. Such a key is reported as ignored.
  if (out.source_version < 4 && root.isMember("role_bindings") && !root["role_bindings"].empty()) {
    out.dropped.push_back("role_bindings (present in a v" + std::to_string(out.source_version) +
                          " file, which predates the field; ignored, confirm the routing on the form)");
  }
  for (const auto &value : (out.source_version >= 4 ? root["role_bindings"] : Json::Value(Json::arrayValue))) {
    RoleBinding binding;
    binding.role = value.get("role", "").asString();
    binding.trigger_channel_id = value.get("trigger_channel_id", "").asString();
    parsed.role_bindings.push_back(binding);
  }

  // Every profile leaves here at the current version, whatever it arrived as.
  parsed.schema_version = kProfileSchemaVersion;

  // v5 removed defaults.report_formats. Reported with its old value, so the user can
  // see what was discarded rather than just "something changed". This is the only
  // difference a valid v4 profile has from v5, which is what makes the upgrade
  // lossless (see below).
  bool dropped_report_formats = false;
  if (out.source_version <= 4 && root["defaults"].isMember("report_formats")) {
    std::string listed;
    for (const auto &value : root["defaults"]["report_formats"]) {
      const std::string name = value.asString();
      if (name.empty()) {
        continue;
      }
      if (!listed.empty()) {
        listed += ",";
      }
      listed += name;
    }
    out.dropped.push_back("defaults.report_formats" + (listed.empty() ? std::string() : "=[" + listed + "]") +
                          " (v5 removed the field: every run writes html, json and markdown)");
    dropped_report_formats = true;
  }

  collect_dropped(root, out.source_version, &out);
  collect_required(parsed, &out);
  collect_invalid(parsed, &out);

  if (out.state == ConfigVersionState::Legacy) {
    out.migrated.push_back("schema_version " + std::to_string(out.source_version) + " -> " +
                           std::to_string(kProfileSchemaVersion));

    // A v4 -> v5 step whose ONLY difference is the dropped report_formats needs
    // nothing from the user, so the profile stays runnable (plan 2.10.2).
    //
    // Deliberately narrow: `dropped` must contain nothing else. That single
    // condition is what keeps `enabled: false` (adds its own dropped entry) and an
    // incomplete routing (adds a required entry) outside the exception -- rather than
    // a list of special cases that could fall out of date.
    //
    // `source_version == 4` is belt-and-braces and does not currently change any
    // outcome: a v2/v3 file's role_bindings predates the field, so it is ignored and
    // leaves a required entry, which already blocks it (verified by removing the
    // condition -- v2 and v3 still came back not-lossless). Kept because the
    // exception is defined for v4 -> v5 only, and the field-reading rules that make
    // the other versions fail are not the rule being expressed here.
    const bool only_version_migrated = out.migrated.size() == 1;
    const bool nothing_else_dropped = out.dropped.size() == (dropped_report_formats ? 1u : 0u);
    out.lossless_upgrade = out.source_version == 4 && only_version_migrated && nothing_else_dropped &&
                           out.required.empty() && out.invalid.empty();
  }

  // Parsing succeeded, so there are values to prefill a form with. This is set
  // regardless of state: a Current config that fails field validation needs the
  // draft just as much as a Legacy one, and it used to be derived from Legacy
  // alone, which left the form empty for it.
  out.draft_available = true;

  if (profile != nullptr) {
    *profile = std::move(parsed);
  }
  return true;
}

bool migrate_profile_file(const std::string &path, DeviceProfile *profile, MigrationReport *report) {
  MigrationReport local;
  MigrationReport &out = report != nullptr ? *report : local;
  out = MigrationReport();
  out.target_version = kProfileSchemaVersion;

  std::ifstream in(path);
  if (!in) {
    out.state = ConfigVersionState::Malformed;
    out.error = "config file could not be read";
    return false;
  }
  Json::CharReaderBuilder builder;
  Json::Value root;
  std::string errors;
  if (!Json::parseFromStream(builder, in, &root, &errors)) {
    out.state = ConfigVersionState::Malformed;
    out.error = "config file is not valid JSON: " + errors;
    return false;
  }
  return migrate_profile_json(root, profile, &out);
}

Json::Value migration_report_to_json(const MigrationReport &report) {
  Json::Value out(Json::objectValue);
  out["state"] = to_string(report.state);
  out["source_version"] = report.source_version;
  out["target_version"] = report.target_version;
  // Always arrays, never absent: a missing key becomes undefined.map() in the UI.
  out["migrated"] = Json::Value(Json::arrayValue);
  out["dropped"] = Json::Value(Json::arrayValue);
  out["required"] = Json::Value(Json::arrayValue);
  out["invalid"] = Json::Value(Json::arrayValue);
  for (const auto &item : report.migrated) {
    out["migrated"].append(item);
  }
  for (const auto &item : report.dropped) {
    out["dropped"].append(item);
  }
  for (const auto &item : report.required) {
    out["required"].append(item);
  }
  for (const auto &item : report.invalid) {
    out["invalid"].append(item);
  }
  out["usable_for_run"] = report.usable_for_run();
  out["needs_user_input"] = report.needs_user_input();
  out["has_draft"] = report.has_draft();
  out["error"] = report.error;
  return out;
}

RunsIndexState classify_runs_index(const Json::Value &root) {
  RunsIndexState state;
  state.target_version = kRunsIndexSchemaVersion;
  if (root.isArray()) {
    // What older builds wrote: a bare array with no version. Readable, and it
    // gains the version the next time the index is written.
    state.state = ConfigVersionState::Legacy;
    state.source_version = 0;
    return state;
  }
  int version = 0;
  if (!root.isObject() || !read_version(root, &version)) {
    state.state = ConfigVersionState::Malformed;
    return state;
  }
  state.source_version = version;
  if (version == kRunsIndexSchemaVersion) {
    // A current document must actually carry the array; otherwise it is as
    // unreadable as a corrupt file and must not be overwritten.
    state.state = root["runs"].isArray() ? ConfigVersionState::Current : ConfigVersionState::Malformed;
  } else {
    state.state = version < kRunsIndexSchemaVersion ? ConfigVersionState::Legacy : ConfigVersionState::Future;
  }
  return state;
}

}  // namespace v4l2diag
