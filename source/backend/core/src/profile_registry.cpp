#include "v4l2diag/core/profile_registry.hpp"

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

ControlDeviceSelector::Kind selector_kind_from_string(const std::string &value) {
  if (value == "video") {
    return ControlDeviceSelector::Kind::VideoDevice;
  }
  if (value == "subdevice") {
    return ControlDeviceSelector::Kind::SubDevice;
  }
  return ControlDeviceSelector::Kind::CaptureDevice;
}

Json::Value matcher_to_json(const CameraMatcher &matcher) {
  Json::Value out(Json::objectValue);
  out["driver"] = matcher.driver;
  out["card"] = matcher.card;
  out["bus_info"] = matcher.bus_info;
  return out;
}

CameraMatcher matcher_from_json(const Json::Value &root) {
  CameraMatcher matcher;
  matcher.driver = root.get("driver", "").asString();
  matcher.card = root.get("card", "").asString();
  matcher.bus_info = root.get("bus_info", "").asString();
  return matcher;
}

Json::Value control_write_to_json(const V4l2ControlWrite &write) {
  Json::Value out(Json::objectValue);
  out["id"] = Json::UInt(write.id);
  out["name"] = write.name;
  out["type"] = Json::UInt(write.type);
  out["value"] = Json::Int64(write.value);
  return out;
}

V4l2ControlWrite control_write_from_json(const Json::Value &root) {
  V4l2ControlWrite write;
  write.id = root.get("id", 0).asUInt();
  write.name = root.get("name", "").asString();
  write.type = root.get("type", 0).asUInt();
  write.value = root.get("value", 0).asInt64();
  return write;
}

Json::Value profile_to_json(const DeviceProfile &profile) {
  Json::Value root(Json::objectValue);
  root["schema_version"] = 2;
  root["id"] = profile.id;
  root["name"] = profile.name;
  root["description"] = profile.description;
  root["enabled"] = profile.enabled;
  root["camera_match"] = matcher_to_json(profile.camera_match);

  Json::Value defaults(Json::objectValue);
  defaults["trigger_mode"] = to_string(profile.defaults.trigger_mode);
  for (MemoryBackend backend : profile.defaults.memory_backends) {
    defaults["memory_backends"].append(to_string(backend));
  }
  for (const auto &selector : profile.defaults.test_selectors) {
    defaults["test_selectors"].append(selector);
  }
  for (ReportFormat format : profile.defaults.report_formats) {
    defaults["report_formats"].append(to_string(format));
  }
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

  for (const auto &binding : profile.camera_bindings) {
    Json::Value item(Json::objectValue);
    item["camera"] = matcher_to_json(binding.camera);
    item["trigger_channel_id"] = binding.trigger_channel_id;
    root["camera_bindings"].append(item);
  }
  return root;
}

bool parse_json_profile(const std::string &path, DeviceProfile *profile) {
  std::ifstream in(path);
  if (!in) {
    return false;
  }
  Json::CharReaderBuilder builder;
  Json::Value root;
  std::string errors;
  if (!Json::parseFromStream(builder, in, &root, &errors) || !root.isObject()) {
    return false;
  }

  DeviceProfile parsed;
  parsed.schema_version = root.get("schema_version", 2).asInt();
  parsed.id = root.get("id", "").asString();
  parsed.name = root.get("name", parsed.id).asString();
  parsed.description = root.get("description", "").asString();
  parsed.enabled = root.get("enabled", true).asBool();
  parsed.camera_match = matcher_from_json(root["camera_match"]);

  const Json::Value &defaults = root["defaults"];
  parse_trigger_mode(defaults.get("trigger_mode", "free-run").asString(), &parsed.defaults.trigger_mode);
  for (const auto &value : defaults["memory_backends"]) {
    MemoryBackend backend;
    if (parse_memory_backend(value.asString(), &backend)) {
      parsed.defaults.memory_backends.push_back(backend);
    }
  }
  for (const auto &value : defaults["test_selectors"]) {
    parsed.defaults.test_selectors.push_back(value.asString());
  }
  for (const auto &value : defaults["report_formats"]) {
    ReportFormat format;
    if (parse_report_format(value.asString(), &format)) {
      parsed.defaults.report_formats.push_back(format);
    }
  }

  for (const auto &value : root["trigger_channels"]) {
    TriggerChannel channel;
    channel.id = value.get("id", "").asString();
    channel.name = value.get("name", "").asString();
    channel.description = value.get("description", "").asString();
    channel.type = value.get("type", "hardware").asString() == "software" ? TriggerChannel::Type::Software
                                                                          : TriggerChannel::Type::Hardware;
    if (channel.type == TriggerChannel::Type::Hardware) {
      channel.gpio.chip_id = value["gpio"].get("chip_id", 0).asInt();
      channel.gpio.line_number = value["gpio"].get("line_number", 0).asInt();
      channel.gpio.description = value["gpio"].get("description", "").asString();
    } else {
      const Json::Value &selector = value["control_device"];
      channel.control_device.kind = selector_kind_from_string(selector.get("kind", "capture").asString());
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

  for (const auto &value : root["camera_bindings"]) {
    CameraBinding binding;
    binding.camera = matcher_from_json(value["camera"]);
    binding.trigger_channel_id = value.get("trigger_channel_id", "").asString();
    parsed.camera_bindings.push_back(binding);
  }

  if (!valid_id(parsed.id)) {
    return false;
  }
  *profile = std::move(parsed);
  return true;
}

}  // namespace

bool validate_device_profile(const DeviceProfile &profile, std::string *error) {
  auto fail = [&](const std::string &message) {
    if (error) {
      *error = message;
    }
    return false;
  };
  if (profile.schema_version != 2) {
    return fail("unsupported profile schema version");
  }
  if (!valid_id(profile.id)) {
    return fail("profile id must contain only lowercase letters, digits, '-' or '_'");
  }
  if (profile.name.empty()) {
    return fail("profile name is required");
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
  for (const auto &binding : profile.camera_bindings) {
    if (channel_ids.count(binding.trigger_channel_id) == 0) {
      return fail("camera binding references an unknown trigger channel");
    }
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
  std::vector<DeviceProfile> enabled;
  for (const auto &profile : profiles_) {
    if (profile.enabled) {
      enabled.push_back(profile);
    }
  }
  std::sort(enabled.begin(), enabled.end(), [](const DeviceProfile &a, const DeviceProfile &b) { return a.id < b.id; });
  return enabled;
}

bool ProfileRegistry::get_profile(const std::string &id, DeviceProfile *profile) const {
  const auto it = std::find_if(profiles_.begin(), profiles_.end(),
                               [&](const DeviceProfile &candidate) { return candidate.id == id && candidate.enabled; });
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

void ProfileRegistry::load() {
  profiles_.clear();
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
    const bool parsed = parse_json_profile(path, &user_profile);
    if (!parsed) {
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
