#include "v4l2diag/core/threshold_registry.hpp"

#include <json/json.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
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

bool ends_with(const std::string &value, const std::string &suffix) {
  return value.size() >= suffix.size() && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

Json::Value config_to_json(const ThresholdConfig &config) {
  Json::Value root(Json::objectValue);
  root["schema_version"] = 1;
  root["id"] = config.id;
  root["name"] = config.name;
  root["description"] = config.description;
  Json::Value values(Json::objectValue);
  for (const auto &test : config.values) {
    Json::Value keys(Json::objectValue);
    for (const auto &kv : test.second) {
      keys[kv.first] = kv.second;
    }
    values[test.first] = keys;
  }
  root["values"] = values;
  return root;
}

// Parses a config from JSON. When `known_only` is set, test ids and keys that
// are not part of the built-in default are silently dropped (forward/backward
// compatibility on import).
bool config_from_json(const Json::Value &root, bool known_only, ThresholdConfig *config) {
  if (!root.isObject()) {
    return false;
  }
  const ThresholdConfig defaults = default_threshold_config();
  ThresholdConfig parsed;
  parsed.id = root.get("id", "").asString();
  parsed.name = root.get("name", parsed.id).asString();
  parsed.description = root.get("description", "").asString();

  const Json::Value &values = root["values"];
  if (values.isObject()) {
    for (const auto &test_id : values.getMemberNames()) {
      const bool known_test = defaults.values.count(test_id) != 0;
      if (known_only && !known_test) {
        continue;
      }
      const Json::Value &keys = values[test_id];
      if (!keys.isObject()) {
        continue;
      }
      for (const auto &key : keys.getMemberNames()) {
        if (known_only && (!known_test || defaults.values.at(test_id).count(key) == 0)) {
          continue;
        }
        if (!keys[key].isNumeric()) {
          continue;
        }
        parsed.values[test_id][key] = keys[key].asDouble();
      }
    }
  }

  if (!valid_id(parsed.id)) {
    return false;
  }
  *config = std::move(parsed);
  return true;
}

bool parse_config_file(const std::string &path, ThresholdConfig *config) {
  std::ifstream in(path);
  if (!in) {
    return false;
  }
  Json::CharReaderBuilder builder;
  Json::Value root;
  std::string errors;
  if (!Json::parseFromStream(builder, in, &root, &errors)) {
    return false;
  }
  return config_from_json(root, /*known_only=*/false, config);
}

}  // namespace

double ThresholdConfig::get(const std::string &test_id, const std::string &key) const {
  auto test_it = values.find(test_id);
  if (test_it != values.end()) {
    auto key_it = test_it->second.find(key);
    if (key_it != test_it->second.end()) {
      return key_it->second;
    }
  }
  const ThresholdConfig defaults = default_threshold_config();
  auto dt = defaults.values.find(test_id);
  if (dt != defaults.values.end()) {
    auto dk = dt->second.find(key);
    if (dk != dt->second.end()) {
      return dk->second;
    }
  }
  return 0.0;
}

ThresholdConfig default_threshold_config() {
  ThresholdConfig config;
  config.id = "default";
  config.name = "Default";
  config.description = "Built-in default verdict thresholds (conservative, matches historical behavior).";
  config.values = {
      {"t02-buffer-overwrite", {{"max_error_flags", 0}}},
      {"t07-poll-timeout-sweep", {{"production_timeout_ms", 48.5}, {"safe_margin_ms", 5.0}}},
      {"t08-sequence-continuity", {{"max_dropped_frames", 5}, {"max_non_monotonic", 0}}},
      {"t09-sustained-capture",
       {{"pass_rate_pct", 95.0}, {"warn_rate_pct", 80.0}, {"pass_drift_ms", 1.0}, {"warn_drift_ms", 5.0}}},
      {"t11-buffer-recycling", {{"min_safe_cliff_delay_ms", 50}}},
      {"t12-stream-cycles",
       {{"max_full_failures_pass", 0},
        {"max_full_failures_warn", 2},
        {"rapid_pct_pass", 90.0},
        {"rapid_pct_warn", 70.0}}},
      {"t13-buffer-flags", {{"max_error_flags", 0}}},
      {"t14-timestamp-monotonicity", {{"max_non_monotonic", 0}}},
      {"t17-pollerr-handling", {{"min_recovery_ok", 2}}},
      {"t18-dmabuf-cache-sync", {{"min_match_ratio", 1.0}}},
      {"t21-stuck-frame", {{"max_identical_run", 5}}},
      {"t22-latency-under-load", {{"pass_delta_p95_ms", 5.0}, {"warn_delta_p95_ms", 20.0}}},
  };
  return config;
}

std::string default_threshold_directory() {
  const char *xdg = std::getenv("XDG_CONFIG_HOME");
  if (xdg && *xdg) {
    return std::string(xdg) + "/v4l2-camera-diagnostic/thresholds";
  }
  const char *home = std::getenv("HOME");
  if (home && *home) {
    return std::string(home) + "/.config/v4l2-camera-diagnostic/thresholds";
  }
  return ".v4l2-camera-diagnostic/thresholds";
}

bool validate_threshold_config(const ThresholdConfig &config, std::string *error) {
  auto fail = [&](const std::string &message) {
    if (error) {
      *error = message;
    }
    return false;
  };
  if (!valid_id(config.id)) {
    return fail("threshold config id must contain only lowercase letters, digits, '-' or '_'");
  }
  if (config.name.empty()) {
    return fail("threshold config name is required");
  }
  for (const auto &test : config.values) {
    for (const auto &kv : test.second) {
      if (!std::isfinite(kv.second)) {
        return fail("threshold '" + test.first + "." + kv.first + "' must be a finite number");
      }
      if (kv.first.find("pct") != std::string::npos) {
        if (kv.second < 0.0 || kv.second > 100.0) {
          return fail("percentage threshold '" + test.first + "." + kv.first + "' must be between 0 and 100");
        }
      } else if (kv.second < 0.0) {
        return fail("threshold '" + test.first + "." + kv.first + "' must not be negative");
      }
    }
  }
  return true;
}

ThresholdRegistry::ThresholdRegistry(std::string directory)
    : directory_(directory.empty() ? default_threshold_directory() : std::move(directory)) {
  seed_default_file();
  load();
}

std::vector<ThresholdConfig> ThresholdRegistry::list_configs() const {
  std::vector<ThresholdConfig> out = configs_;
  const bool has_default =
      std::any_of(out.begin(), out.end(), [](const ThresholdConfig &c) { return c.id == "default"; });
  if (!has_default) {
    out.push_back(default_threshold_config());
  }
  std::sort(out.begin(), out.end(), [](const ThresholdConfig &a, const ThresholdConfig &b) {
    // "default" always first, then alphabetical.
    if ((a.id == "default") != (b.id == "default")) {
      return a.id == "default";
    }
    return a.id < b.id;
  });
  return out;
}

bool ThresholdRegistry::get_config(const std::string &id, ThresholdConfig *config) const {
  const auto it = std::find_if(configs_.begin(), configs_.end(),
                               [&](const ThresholdConfig &candidate) { return candidate.id == id; });
  if (it != configs_.end()) {
    *config = *it;
    return true;
  }
  if (id == "default") {
    *config = default_threshold_config();
    return true;
  }
  return false;
}

bool ThresholdRegistry::add_or_update_config(const ThresholdConfig &config, std::string *error) {
  if (!validate_threshold_config(config, error) || !write_config_file(config, error)) {
    return false;
  }
  load();
  return true;
}

bool ThresholdRegistry::remove_config(const std::string &id, std::string *error) {
  if (id == "default") {
    if (error) {
      *error = "the default threshold config cannot be removed";
    }
    return false;
  }
  if (!valid_id(id)) {
    if (error) {
      *error = "invalid threshold config id";
    }
    return false;
  }
  const std::string path = config_path(id);
  if (unlink(path.c_str()) != 0 && errno != ENOENT) {
    if (error) {
      *error = std::string("failed to remove threshold config: ") + std::strerror(errno);
    }
    return false;
  }
  load();
  return true;
}

bool ThresholdRegistry::import_config(const std::string &json_text, std::string *error) {
  Json::CharReaderBuilder builder;
  Json::Value root;
  std::string errors;
  std::istringstream in(json_text);
  if (!Json::parseFromStream(builder, in, &root, &errors)) {
    if (error) {
      *error = "invalid JSON: " + errors;
    }
    return false;
  }
  ThresholdConfig config;
  if (!config_from_json(root, /*known_only=*/true, &config)) {
    if (error) {
      *error = "config must have a valid id";
    }
    return false;
  }
  return add_or_update_config(config, error);
}

bool ThresholdRegistry::export_config(const std::string &id, std::string *json_text) const {
  ThresholdConfig config;
  if (!get_config(id, &config)) {
    return false;
  }
  Json::StreamWriterBuilder builder;
  builder["indentation"] = "  ";
  *json_text = Json::writeString(builder, config_to_json(config));
  return true;
}

ThresholdConfig ThresholdRegistry::resolve(const std::string &id) const {
  ThresholdConfig base = default_threshold_config();
  ThresholdConfig requested;
  if (get_config(id, &requested)) {
    for (const auto &test : requested.values) {
      auto base_test = base.values.find(test.first);
      if (base_test == base.values.end()) {
        continue;  // unknown test id — ignore
      }
      for (const auto &kv : test.second) {
        if (base_test->second.count(kv.first) != 0) {
          base_test->second[kv.first] = kv.second;  // only overlay known keys
        }
      }
    }
    base.id = requested.id;
    base.name = requested.name;
    base.description = requested.description;
  }
  return base;
}

void ThresholdRegistry::load() {
  configs_.clear();
  DIR *dir = opendir(directory_.c_str());
  if (!dir) {
    return;
  }
  std::vector<std::string> names;
  while (dirent *entry = readdir(dir)) {
    const std::string name = entry->d_name;
    if (ends_with(name, ".json")) {
      names.push_back(name);
    }
  }
  closedir(dir);
  std::sort(names.begin(), names.end());

  for (const auto &name : names) {
    ThresholdConfig config;
    if (!parse_config_file(directory_ + "/" + name, &config)) {
      continue;
    }
    auto it = std::find_if(configs_.begin(), configs_.end(),
                           [&](const ThresholdConfig &existing) { return existing.id == config.id; });
    if (it == configs_.end()) {
      configs_.push_back(std::move(config));
    } else {
      *it = std::move(config);
    }
  }
}

void ThresholdRegistry::seed_default_file() const {
  const std::string path = config_path("default");
  std::ifstream existing(path);
  if (existing.good()) {
    return;  // already present; do not overwrite a user-inspected copy
  }
  std::string ignored;
  write_config_file(default_threshold_config(), &ignored);  // best-effort
}

bool ThresholdRegistry::write_config_file(const ThresholdConfig &config, std::string *error) const {
  if (!ensure_directory(directory_)) {
    if (error) {
      *error = "failed to create threshold directory";
    }
    return false;
  }
  const std::string path = config_path(config.id);
  const std::string temporary = path + ".tmp-" + std::to_string(getpid());
  std::ofstream out(temporary);
  if (!out) {
    if (error) {
      *error = "failed to open threshold file for writing";
    }
    return false;
  }
  Json::StreamWriterBuilder builder;
  builder["indentation"] = "  ";
  out << Json::writeString(builder, config_to_json(config)) << "\n";
  out.close();
  if (!out || rename(temporary.c_str(), path.c_str()) != 0) {
    unlink(temporary.c_str());
    if (error) {
      *error = std::string("failed to replace threshold file: ") + std::strerror(errno);
    }
    return false;
  }
  return true;
}

std::string ThresholdRegistry::config_path(const std::string &id) const {
  return directory_ + "/" + id + ".json";
}

}  // namespace v4l2diag
