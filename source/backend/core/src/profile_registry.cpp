#include "v4l2diag/core/profile_registry.hpp"

#include "v4l2diag/core/config_migration.hpp"

#include "v4l2diag/core/types.hpp"

#include <json/json.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <dirent.h>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace v4l2diag {

namespace {

bool ensure_directory(const std::string &path) {
  if (path.empty()) {
    return false;
  }
  std::string partial;
  for (char c : path) {
    partial.push_back(c);
    if (c == '/' && partial.size() > 1) {
      mkdir(partial.c_str(), 0755);
    }
  }
  return mkdir(path.c_str(), 0755) == 0 || errno == EEXIST;
}

bool valid_id(const std::string &id) {
  if (id.empty()) {
    return false;
  }
  return std::all_of(id.begin(), id.end(), [](unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
  });
}

const char *selector_kind_to_string(ControlDeviceSelector::Kind kind) {
  switch (kind) {
    case ControlDeviceSelector::Kind::CaptureDevice:
      return "capture";
    case ControlDeviceSelector::Kind::VideoDevice:
      return "video";
    case ControlDeviceSelector::Kind::SubDevice:
      return "subdevice";
  }
  return "capture";
}

Json::Value control_write_to_json(const V4l2ControlWrite &write) {
  Json::Value out(Json::objectValue);
  out["id"] = Json::UInt(write.id);
  out["name"] = write.name;
  out["type"] = Json::UInt(write.type);
  out["value"] = Json::Int64(write.value);
  return out;
}

Json::Value profile_to_json(const DeviceProfile &profile) {
  Json::Value root(Json::objectValue);
  root["schema_version"] = kProfileSchemaVersion;
  root["id"] = profile.id;
  root["name"] = profile.name;
  root["description"] = profile.description;

  Json::Value defaults(Json::objectValue);
  defaults["trigger_mode"] = to_string(profile.defaults.trigger_mode);
  for (MemoryBackend backend : profile.defaults.memory_backends) {
    defaults["memory_backends"].append(to_string(backend));
  }
  for (const auto &selector : profile.defaults.test_selectors) {
    defaults["test_selectors"].append(selector);
  }
  defaults["trigger_rate_hz"] = profile.defaults.trigger_rate_hz;
  defaults["pulse_width_ms"] = profile.defaults.pulse_width_ms;
  root["defaults"] = defaults;

  for (const auto &channel : profile.trigger_channels) {
    Json::Value item(Json::objectValue);
    item["id"] = channel.id;
    item["name"] = channel.name;
    item["description"] = channel.description;
    item["type"] = channel.type == TriggerChannel::Type::Hardware ? "hardware" : "software";
    if (channel.type == TriggerChannel::Type::Hardware) {
      item["gpio"]["chip_id"] = channel.gpio.chip_id;
      item["gpio"]["line_number"] = channel.gpio.line_number;
      item["gpio"]["description"] = channel.gpio.description;
    } else {
      item["control_device"]["kind"] = selector_kind_to_string(channel.control_device.kind);
      item["control_device"]["driver"] = channel.control_device.driver;
      item["control_device"]["card"] = channel.control_device.card;
      item["control_device"]["bus_info"] = channel.control_device.bus_info;
      item["control_device"]["sysfs_name"] = channel.control_device.sysfs_name;
      for (const auto &write : channel.setup_controls) {
        item["setup"].append(control_write_to_json(write));
      }
      for (const auto &write : channel.fire_controls) {
        item["fire"].append(control_write_to_json(write));
      }
      for (const auto &write : channel.teardown_controls) {
        item["teardown"].append(control_write_to_json(write));
      }
    }
    root["trigger_channels"].append(item);
  }

  // Always an array, even when empty: a free-run profile legitimately routes
  // nothing, and an absent key becomes undefined.map() in the UI.
  root["role_bindings"] = Json::Value(Json::arrayValue);
  for (const auto &binding : profile.role_bindings) {
    Json::Value item(Json::objectValue);
    item["role"] = binding.role;
    item["trigger_channel_id"] = binding.trigger_channel_id;
    root["role_bindings"].append(item);
  }
  return root;
}

}  // namespace

bool validate_device_profile(const DeviceProfile &profile, std::string *error) {
  auto fail = [&](const std::string &message) {
    if (error) {
      *error = message;
    }
    return false;
  };
  if (profile.schema_version != kProfileSchemaVersion) {
    return fail("unsupported profile schema version");
  }
  if (!valid_id(profile.id)) {
    return fail("profile id must contain only lowercase letters, digits, '-' or '_'");
  }
  if (profile.name.empty()) {
    return fail("profile name is required");
  }
  if (profile.trigger_channels.empty()) {
    // Every save path shares this rule. It used to live only in the migration
    // contract's collect_required(), so a channel-less profile passed validation
    // here and was written, then came straight back as "migration required".
    return fail("at least one trigger channel is required");
  }
  std::set<std::string> channel_ids;
  for (const auto &channel : profile.trigger_channels) {
    if (!valid_id(channel.id) || !channel_ids.insert(channel.id).second) {
      return fail("trigger channel ids must be unique valid identifiers");
    }
    if (channel.type == TriggerChannel::Type::Hardware && channel.gpio.line_number < 0) {
      return fail("GPIO line number cannot be negative");
    }
    if (channel.type == TriggerChannel::Type::Software && channel.fire_controls.empty()) {
      return fail("software trigger channel requires at least one fire control");
    }
  }
  // Same rules as the migration contract's collect_invalid(), so a profile cannot
  // pass one and fail the other. The run-topology check lives in
  // resolve_role_bindings() and runs per run.
  std::set<std::string> bound_roles;
  for (const auto &binding : profile.role_bindings) {
    if (channel_ids.count(binding.trigger_channel_id) == 0) {
      return fail("role binding references an unknown trigger channel");
    }
    if (!bound_roles.insert(binding.role).second) {
      return fail("a role may be bound only once");
    }
    if (!is_canonical_role(binding.role)) {
      return fail("role must be one of master, slave-1, slave-2, ...");
    }
  }
  if (profile.defaults.trigger_mode != TriggerMode::FreeRun && bound_roles.count(kMasterRole()) == 0) {
    // Specifically master, not merely "some binding": every run has a master, so a
    // slave-only profile can never start one. Refusing it at save time is better
    // than storing something guaranteed to fail later.
    return fail("a triggered profile must bind the master role");
  }
  if (profile.defaults.trigger_rate_hz <= 0 || profile.defaults.trigger_rate_hz > 1000) {
    return fail("trigger_rate_hz must be > 0 and <= 1000");
  }
  if (profile.defaults.pulse_width_ms <= 0 || profile.defaults.pulse_width_ms > 100) {
    return fail("pulse_width_ms must be > 0 and <= 100");
  }
  return true;
}

std::string default_config_directory() {
  const char *xdg = std::getenv("XDG_CONFIG_HOME");
  if (xdg && *xdg) {
    return std::string(xdg) + "/v4l2-camera-diagnostic/profiles";
  }
  const char *home = std::getenv("HOME");
  if (home && *home) {
    return std::string(home) + "/.config/v4l2-camera-diagnostic/profiles";
  }
  return ".v4l2-camera-diagnostic/profiles";
}

ProfileRegistry::ProfileRegistry(std::string config_directory)
    : config_directory_(config_directory.empty() ? default_config_directory() : std::move(config_directory)) {
  load();
}

std::vector<DeviceProfile> ProfileRegistry::list_profiles() const {
  std::vector<DeviceProfile> out = profiles_;
  std::sort(out.begin(), out.end(), [](const DeviceProfile &a, const DeviceProfile &b) { return a.id < b.id; });
  return out;
}

bool ProfileRegistry::get_profile(const std::string &id, DeviceProfile *profile) const {
  const auto it = std::find_if(profiles_.begin(), profiles_.end(),
                               [&](const DeviceProfile &candidate) { return candidate.id == id; });
  if (it == profiles_.end()) {
    return false;
  }
  *profile = *it;
  return true;
}

bool ProfileRegistry::add_or_update_profile(const DeviceProfile &profile, std::string *error) {
  if (!validate_device_profile(profile, error) || !write_profile_file(profile, error)) {
    return false;
  }
  load();
  return true;
}

bool ProfileRegistry::migrate_config(const std::string &source_file, const DeviceProfile &profile, std::string *error) {
  // First of three gates: membership, then eligibility, then target collisions.
  //
  // The name has to match a file this registry actually enumerated from its own
  // directory, so "../" or an absolute path cannot get through -- there is no
  // string sanitising to get subtly wrong.
  const auto it = std::find_if(stored_configs_.begin(), stored_configs_.end(),
                               [&](const StoredConfig &stored) { return stored.file == source_file; });
  if (it == stored_configs_.end()) {
    if (error) {
      *error = "unknown config file";
    }
    return false;
  }
  // Only a config that actually needs repairing may be committed through here.
  // Membership alone was not enough: a valid current body aimed at a Future,
  // Malformed or already-runnable config would rewrite or delete that file.
  //
  //   has_draft()        -- it parsed, so there is something to migrate
  //   needs_user_input() -- it is Legacy or has required/invalid fields
  //
  // Together these exclude Future and Malformed (neither parses, so no draft) and
  // a runnable Current config (nothing for the user to supply).
  if (!it->report.has_draft() || !it->report.needs_user_input()) {
    if (error) {
      *error = it->report.usable_for_run() ? "this config does not need migration; use POST or PUT /api/profiles"
                                           : "this config cannot be migrated: it could not be read";
    }
    return false;
  }
  if (!validate_device_profile(profile, error)) {
    return false;
  }

  const std::string source_path = config_directory_ + "/" + source_file;
  const std::string target_path = profile_path(profile.id);
  const bool in_place = target_path == source_path;

  // Collision checks run against every stored config, not just the runnable ones:
  // get_profile() reports nothing for a Future, Legacy or Malformed file, so such a
  // file used to be overwritten silently.
  //
  // The two checks have different scopes, which is the part that was wrong before:
  //
  //   target path -- only when the write lands somewhere else. An in-place write
  //                  legitimately replaces its own file.
  //   profile id  -- ALWAYS. An in-place migration can still promote a second
  //                  config to a profile id another file already defines, leaving
  //                  two configs claiming the same id and load order deciding
  //                  which one wins. Skipping this for in_place made the whole
  //                  loop dead in exactly that case.
  for (const auto &stored : stored_configs_) {
    if (stored.file == source_file) {
      continue;
    }
    if (!in_place && config_directory_ + "/" + stored.file == target_path) {
      if (error) {
        *error = "another config file (" + stored.file + ") already occupies the target path";
      }
      return false;
    }
    if (!stored.profile_id.empty() && stored.profile_id == profile.id) {
      if (error) {
        *error = "another config (" + stored.file + ") already defines profile \"" + profile.id + "\"";
      }
      return false;
    }
  }

  if (!write_profile_file(profile, error)) {
    return false;
  }
  // The rename case: the migrated profile lives somewhere else now, so the source
  // has to go, or it keeps being reported as needing migration.
  //
  // Two files are involved, so this is not one atomic step. If the source cannot
  // be removed, a rollback of the new target is attempted; when it succeeds the
  // source is left as the only config for this profile -- the state the caller
  // started from. Better that than leaving both on disk with no way to tell which
  // one is authoritative. The rollback itself can fail; see below.
  if (!in_place && unlink(source_path.c_str()) != 0 && errno != ENOENT) {
    const std::string reason = std::strerror(errno);
    // The rollback can fail too. Claiming "rolled back" without checking would be
    // the worst of the three outcomes: both files on disk and a message saying
    // otherwise. Name both paths so the operator can resolve it by hand.
    //
    // Not covered by a test: reaching this needs the source unlink to fail while
    // the target unlink also fails, in the directory that just accepted the write.
    // That is not constructible without fault injection, so this branch is
    // reasoned-about rather than exercised -- see tests/config_migration_test.cpp,
    // which covers the successful-rollback branch only.
    const bool rolled_back = unlink(target_path.c_str()) == 0 || errno == ENOENT;
    if (error) {
      *error = rolled_back ? "could not remove the original config (" + reason + "); the migration was rolled back"
                           : "could not remove the original config (" + reason +
                                 ") and the new one could not be rolled back; "
                                 "both " +
                                 source_path + " and " + target_path + " are now on disk and must be resolved by hand";
    }
    load();
    return false;
  }
  load();
  return true;
}

bool ProfileRegistry::remove_profile(const std::string &id, std::string *error) {
  if (!valid_id(id)) {
    if (error) {
      *error = "invalid profile id";
    }
    return false;
  }
  {
    const std::string path = config_directory_ + "/" + id + ".json";
    if (unlink(path.c_str()) != 0 && errno != ENOENT) {
      if (error) {
        *error = std::string("failed to remove profile: ") + std::strerror(errno);
      }
      return false;
    }
  }
  load();
  return true;
}

std::vector<ProfileRegistry::StoredConfig> ProfileRegistry::stored_configs() const {
  return stored_configs_;
}

void ProfileRegistry::load() {
  profiles_.clear();
  stored_configs_.clear();
  load_user_profiles();
}

void ProfileRegistry::load_user_profiles() {
  DIR *dir = opendir(config_directory_.c_str());
  if (!dir) {
    return;
  }
  std::vector<std::string> names;
  while (dirent *entry = readdir(dir)) {
    const std::string name = entry->d_name;
    if (name.size() > 5 && name.substr(name.size() - 5) == ".json") {
      names.push_back(name);
    }
  }
  closedir(dir);
  std::sort(names.begin(), names.end());

  for (const auto &name : names) {
    DeviceProfile user_profile;
    const std::string path = config_directory_ + "/" + name;
    // One migration entry point for disk and API alike. Reports are kept even
    // for files that cannot be loaded, so an unusable config is explained
    // instead of silently vanishing from the list.
    MigrationReport report;
    const bool parsed = migrate_profile_file(path, &user_profile, &report);

    StoredConfig stored;
    stored.file = name;
    stored.report = report;
    if (parsed) {
      stored.profile_id = user_profile.id;
      stored.draft = user_profile;
    }
    stored_configs_.push_back(stored);

    // Only configs already at the current schema and free of problems become
    // runnable. A Legacy one is kept as a draft so the UI can prefill its
    // migration form; it becomes runnable when the user saves it explicitly.
    if (!parsed || !report.usable_for_run()) {
      continue;
    }
    auto it = std::find_if(profiles_.begin(), profiles_.end(),
                           [&](const DeviceProfile &profile) { return profile.id == user_profile.id; });
    if (it == profiles_.end()) {
      profiles_.push_back(std::move(user_profile));
    } else {
      *it = std::move(user_profile);
    }
  }
}

bool ProfileRegistry::write_profile_file(const DeviceProfile &profile, std::string *error) const {
  if (!ensure_directory(config_directory_)) {
    if (error) {
      *error = "failed to create config directory";
    }
    return false;
  }
  const std::string path = profile_path(profile.id);
  const std::string temporary = path + ".tmp-" + std::to_string(getpid());
  std::ofstream out(temporary);
  if (!out) {
    if (error) {
      *error = "failed to open profile file for writing";
    }
    return false;
  }
  Json::StreamWriterBuilder builder;
  builder["indentation"] = "  ";
  out << Json::writeString(builder, profile_to_json(profile)) << "\n";
  out.close();
  if (!out || rename(temporary.c_str(), path.c_str()) != 0) {
    unlink(temporary.c_str());
    if (error) {
      *error = std::string("failed to replace profile file: ") + std::strerror(errno);
    }
    return false;
  }
  return true;
}

std::string ProfileRegistry::profile_path(const std::string &id) const {
  return config_directory_ + "/" + id + ".json";
}

}  // namespace v4l2diag
