#include "v4l2diag/core/threshold_registry.hpp"

#include <json/json.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <iostream>
#include <map>
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

bool ends_with(const std::string &value, const std::string &suffix) {
  return value.size() >= suffix.size() && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Strips the "tNN-" prefix from a test id. Test ids get renumbered whenever a
// test is added, merged or removed, so the numeric prefix is not stable across
// versions -- the descriptive suffix is.
std::string test_id_suffix(const std::string &id) {
  const std::size_t dash = id.find('-');
  return dash == std::string::npos ? std::string() : id.substr(dash + 1);
}

// Maps a persisted test id onto a currently-known one.
//
// A config written before a renumbering carries shifted ids: what is now
// "t17-format-comparison" was stored as "t16-format-comparison". Those are
// migrated by matching the suffix, which preserves values the user configured.
// An id with no unique match is dropped -- keeping it made the configuration
// page render cards for tests that no longer exist while hiding every test that
// does, and made the runner silently fall back to built-in defaults because the
// per-test lookup never matched.
std::string migrate_test_id(const std::string &id, const std::set<std::string> &known) {
  if (known.count(id) != 0) {
    return id;
  }
  const std::string suffix = test_id_suffix(id);
  if (suffix.empty()) {
    return std::string();
  }
  std::string match;
  for (const auto &candidate : known) {
    if (test_id_suffix(candidate) != suffix) {
      continue;
    }
    if (!match.empty()) {
      return std::string();  // ambiguous suffix -- refuse to guess
    }
    match = candidate;
  }
  return match;
}

// Every test id that can carry configuration: a test may define run parameters
// without defining verdict thresholds, so neither map alone answers "does this
// test still exist".
const std::set<std::string> &configurable_test_ids() {
  static const std::set<std::string> ids = [] {
    std::set<std::string> result;
    for (const auto &entry : default_threshold_config().values) {
      result.insert(entry.first);
    }
    for (const auto &entry : default_test_params()) {
      result.insert(entry.first);
    }
    return result;
  }();
  return ids;
}

// Reads one test-id-keyed JSON map (either `values` or `params`) into `out`,
// migrating renumbered ids on the way. Ids that already match are applied first
// so a current id is never overwritten by a migrated one. `known` decides whether
// a test exists at all; `defaults` decides which keys are meaningful for this
// particular map.
void collect_test_map(const Json::Value &node, const std::map<std::string, TestThresholds> &defaults,
                      const std::set<std::string> &known, bool known_only, std::map<std::string, TestThresholds> *out,
                      std::vector<std::string> *migrated, std::vector<std::string> *dropped) {
  if (!node.isObject()) {
    return;
  }
  std::vector<std::pair<std::string, std::string>> mapping;
  for (const auto &test_id : node.getMemberNames()) {
    const std::string target = migrate_test_id(test_id, known);
    if (target.empty() || defaults.count(target) == 0) {
      // Either the test is gone, or it exists but defines nothing for this map
      // (a params-only test cannot carry verdict thresholds).
      dropped->push_back(test_id);
      continue;
    }
    if (target != test_id) {
      migrated->push_back(test_id + " -> " + target);
    }
    mapping.emplace_back(test_id, target);
  }
  std::stable_sort(mapping.begin(), mapping.end(),
                   [](const std::pair<std::string, std::string> &lhs, const std::pair<std::string, std::string> &rhs) {
                     return (lhs.first == lhs.second) && (rhs.first != rhs.second);
                   });
  for (const auto &entry : mapping) {
    const Json::Value &keys = node[entry.first];
    if (!keys.isObject()) {
      continue;
    }
    const auto defaults_it = defaults.find(entry.second);
    for (const auto &key : keys.getMemberNames()) {
      if (known_only && (defaults_it == defaults.end() || defaults_it->second.count(key) == 0)) {
        continue;
      }
      if (!keys[key].isNumeric()) {
        continue;
      }
      TestThresholds &slot = (*out)[entry.second];
      if (slot.count(key) != 0) {
        continue;  // an exact-id entry already provided this key
      }
      slot[key] = keys[key].asDouble();
    }
  }
}

Json::Value config_to_json(const ThresholdConfig &config) {
  Json::Value root(Json::objectValue);
  root["schema_version"] = 2;
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
  Json::Value params(Json::objectValue);
  for (const auto &test : config.params) {
    Json::Value keys(Json::objectValue);
    for (const auto &kv : test.second) {
      keys[kv.first] = kv.second;
    }
    params[test.first] = keys;
  }
  root["params"] = params;
  return root;
}

// Parses a config from JSON. Test ids are always reconciled against the built-in
// default: renumbered ids are migrated by suffix and ids with no match are
// dropped, so a stored config can never introduce a test that does not exist.
// When `known_only` is set, individual keys outside the built-in default are
// dropped as well (forward/backward compatibility on import).
bool config_from_json(const Json::Value &root, bool known_only, ThresholdConfig *config,
                      std::vector<std::string> *migrated, std::vector<std::string> *dropped) {
  if (!root.isObject()) {
    return false;
  }
  std::vector<std::string> migrated_local;
  std::vector<std::string> dropped_local;
  std::vector<std::string> &migrated_out = migrated != nullptr ? *migrated : migrated_local;
  std::vector<std::string> &dropped_out = dropped != nullptr ? *dropped : dropped_local;

  const ThresholdConfig defaults = default_threshold_config();
  ThresholdConfig parsed;
  parsed.id = root.get("id", "").asString();
  parsed.name = root.get("name", parsed.id).asString();
  parsed.description = root.get("description", "").asString();

  const std::set<std::string> &known = configurable_test_ids();
  collect_test_map(root["values"], defaults.values, known, known_only, &parsed.values, &migrated_out, &dropped_out);
  collect_test_map(root["params"], default_test_params(), known, known_only, &parsed.params, &migrated_out,
                   &dropped_out);

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
  std::vector<std::string> migrated;
  std::vector<std::string> dropped;
  if (!config_from_json(root, /*known_only=*/false, config, &migrated, &dropped)) {
    return false;
  }
  // Surface the reconciliation: a config that silently loses entries is how a
  // stale file went unnoticed while the UI showed almost no tests.
  if (!migrated.empty() || !dropped.empty()) {
    std::cerr << "v4l2diag: threshold config " << path << " no longer matches the current tests;";
    if (!migrated.empty()) {
      std::cerr << " migrated " << migrated.size() << " (";
      for (std::size_t i = 0; i < migrated.size(); ++i) {
        std::cerr << (i ? ", " : "") << migrated[i];
      }
      std::cerr << ")";
    }
    if (!dropped.empty()) {
      std::cerr << " dropped " << dropped.size() << " (";
      for (std::size_t i = 0; i < dropped.size(); ++i) {
        std::cerr << (i ? ", " : "") << dropped[i];
      }
      std::cerr << ")";
    }
    std::cerr << "; save the preset to rewrite it." << std::endl;
  }
  return true;
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

double ThresholdConfig::get_param(const std::string &test_id, const std::string &key) const {
  auto test_it = params.find(test_id);
  if (test_it != params.end()) {
    auto key_it = test_it->second.find(key);
    if (key_it != test_it->second.end()) {
      return key_it->second;
    }
  }
  const auto defaults = default_test_params();
  auto dt = defaults.find(test_id);
  if (dt != defaults.end()) {
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
      {"t08-buffer-overwrite", {{"max_error_flags", 0}}},
      // production_timeout_ms is the poll() budget this suite recommends for
      // production use; safety_margin_ms reports how far it clears the measured
      // cliff. 48.5ms sat only ~3ms above a 45ms cliff, under safe_margin_ms.
      {"t13-poll-timeout-cliff", {{"production_timeout_ms", 100.0}, {"safe_margin_ms", 5.0}}},
      {"t20-sequence-continuity", {{"max_dropped_frames", 5}, {"max_non_monotonic", 0}}},
      {"t23-sustained-capture",
       {{"pass_rate_pct", 95.0}, {"warn_rate_pct", 80.0}, {"pass_drift_ms", 1.0}, {"warn_drift_ms", 5.0}}},
      {"t09-buffer-recycling", {{"min_safe_cliff_delay_ms", 50}}},
      {"t06-stream-cycles",
       {{"max_full_failures_pass", 0},
        {"max_full_failures_warn", 2},
        {"rapid_pct_pass", 90.0},
        {"rapid_pct_warn", 70.0}}},
      {"t10-buffer-flags", {{"max_error_flags", 0}}},
      {"t21-timestamp-monotonicity", {{"max_non_monotonic", 0}}},
      {"t03-pipeline-ready", {{"pass_first_frame_ms", 500.0}, {"warn_first_frame_ms", 1500.0}}},
      {"t05-pollerr-handling", {{"min_recovery_ok", 2}}},
      {"t12-dmabuf-cache-sync", {{"min_match_ratio", 1.0}}},
      {"t22-stuck-frame", {{"max_identical_run", 5}}},
      {"t24-latency-under-load", {{"pass_delta_p95_ms", 5.0}, {"warn_delta_p95_ms", 20.0}}},
  };
  config.params = default_test_params();
  return config;
}

std::map<std::string, TestThresholds> default_test_params() {
  return {
      {"t03-pipeline-ready",
       {{"buffer_count", 2},
        {"cycles", 3},
        {"settle_ms", 500},
        // Without a ceiling the DQBUF spin never returns if no frame ever
        // arrives.
        {"first_frame_deadline_ms", 3000},
        // Under a hardware/software trigger the sensor emits nothing until a
        // pulse fires, and a pulse sent before the pipeline is ready is simply
        // lost — so keep pulsing at this interval until a frame lands.
        {"trigger_retry_ms", 100},
        // A STREAMON slower than this means the driver is re-initialising a
        // whole shared camera group; one measurement already answers the
        // question, so skip the remaining cycles rather than keep poking it.
        // Zero or negative disables the watchdog.
        {"slow_start_ms", 2000}}},
      {"t04-no-streamon", {{"buffer_count", 2}, {"poll_timeout_ms", 50}}},
      {"t05-pollerr-handling",
       {{"baseline_captures", 3}, {"recovery_captures", 3}, {"poll_timeout_ms", 100}, {"warmup_count", 3}}},
      {"t06-stream-cycles",
       {{"full_cycles", 20},
        {"rapid_cycles", 50},
        {"full_warmup", 3},
        {"full_captures", 5},
        {"full_timeout_ms", 150},
        // A freshly opened session discards its first frame (see t26), so the
        // rapid loop needs a warmup frame of its own before the measured capture.
        {"rapid_warmup", 1},
        {"rapid_timeout_ms", 200},
        // Where sensors share a deserializer and fsync source, STREAMON
        // re-initialises the whole camera group over I2C — that needs far more
        // settle time than the frame interval alone suggests.
        {"rapid_pacing_ms", 250},
        // Safety valves for both loops. Failure counting alone is not enough:
        // failures can alternate with successes and never accumulate
        // consecutively while the hardware still degrades, so a total budget
        // and a duration watchdog back it up.
        {"max_consecutive_start_failures", 3},
        {"max_start_failures", 5},
        // A healthy STREAMON is milliseconds. Seconds means the driver is
        // re-initialising a whole shared camera group over I2C — the earliest
        // reliable signal that rapid cycling is unsafe here, and it appears
        // before the first outright failure.
        {"slow_start_ms", 2000},
        {"max_slow_starts", 3},
        {"recovery_ms", 1000}}},
      {"t07-multi-buffer",
       {{"sample_count", 20},
        {"max_buffers", 5},
        {"warmup_count", 3},
        {"capture_timeout_ms", 100},
        {"sample_interval_ms", 200}}},
      {"t08-buffer-overwrite",
       {{"buffer_count", 2},
        {"variant_a_triggers", 100},
        {"variant_a_interval_ms", 100},
        {"variant_b_triggers", 200},
        {"variant_b_interval_ms", 50},
        {"settle_ms", 500}}},
      {"t09-buffer-recycling",
       {{"reps_per_delay", 10}, {"capture_timeout_ms", 100}, {"inter_rep_interval_ms", 100}, {"warmup_count", 5}}},
      {"t10-buffer-flags",
       {{"sample_count", 50}, {"capture_timeout_ms", 100}, {"sample_interval_ms", 100}, {"warmup_count", 5}}},
      {"t11-memory-throughput", {{"benchmark_reps", 100}}},
      {"t12-dmabuf-cache-sync",
       {{"sample_count", 20}, {"compare_bytes", 64}, {"capture_timeout_ms", 100}, {"warmup_count", 5}}},
      {"t13-poll-timeout-cliff",
       {{"probe_frames", 10}, {"stability_rounds", 5}, {"stability_frames", 10}, {"warmup_count", 10}}},
      {"t14-trigger-latency",
       {{"sample_count", 50}, {"warmup_count", 5}, {"capture_timeout_ms", 100}, {"sample_interval_ms", 200}}},
      {"t15-nonblock-vs-block",
       {{"sample_count", 30},
        {"spin_deadline_ms", 100},
        {"poll_timeout_ms", 200},
        {"sample_interval_ms", 200},
        {"warmup_count", 5}}},
      {"t16-gpio-pulse-width",
       {{"samples_per_width", 8},
        {"warmup_count", 5},
        {"poll_timeout_ms", 500},
        // Min ms by which one edge's across-width latency spread must undercut
        // the other before an edge is declared.
        {"edge_margin_ms", 1.0}}},
      {"t17-format-comparison",
       {{"sample_count", 20}, {"throughput_reps", 50}, {"capture_timeout_ms", 500}, {"warmup_count", 5}}},
      {"t18-control-sweep", {{"warmup_count", 8}, {"sample_count", 20}, {"capture_timeout_ms", 200}}},
      {"t19-resolution-sweep",
       {{"sample_count", 15}, {"throughput_reps", 30}, {"capture_timeout_ms", 500}, {"warmup_count", 3}}},
      {"t20-sequence-continuity",
       {{"sample_count", 100}, {"capture_timeout_ms", 100}, {"sample_interval_ms", 100}, {"warmup_count", 5}}},
      {"t21-timestamp-monotonicity",
       {{"sample_count", 100}, {"capture_timeout_ms", 100}, {"sample_interval_ms", 100}, {"warmup_count", 5}}},
      {"t22-stuck-frame",
       {{"sample_count", 50}, {"compare_bytes", 4096}, {"capture_timeout_ms", 100}, {"warmup_count", 5}}},
      {"t23-sustained-capture",
       {{"duration_sec", 60},
        {"window_sec", 10},
        {"sample_interval_ms", 100},
        {"capture_timeout_ms", 100},
        {"warmup_count", 5}}},
      {"t24-latency-under-load",
       {{"sample_count", 30},
        {"load_threads", 4},
        {"baseline_timeout_ms", 100},
        {"load_timeout_ms", 200},
        {"sample_interval_ms", 200},
        {"warmup_count", 5}}},
      {"t25-multi-camera", {{"sample_count", 50}, {"poll_timeout_ms", 200}, {"warmup_count", 5}}},
      {"t26-cold-start",
       {{"cycles", 10},
        {"max_frames_per_cycle", 30},
        {"stability_threshold_pct", 15.0},
        {"capture_timeout_ms", 500},
        {"inter_frame_interval_ms", 100}}},
  };
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
  for (const auto &test : config.params) {
    for (const auto &kv : test.second) {
      if (!std::isfinite(kv.second)) {
        return fail("param '" + test.first + "." + kv.first + "' must be a finite number");
      }
      if (kv.second < 0.0) {
        return fail("param '" + test.first + "." + kv.first + "' must not be negative");
      }
      const bool is_time =
          kv.first.find("timeout_ms") != std::string::npos || kv.first.find("interval_ms") != std::string::npos ||
          kv.first.find("pacing_ms") != std::string::npos || kv.first.find("recovery_ms") != std::string::npos;
      if (is_time && kv.second > 0.0 && kv.second < 1.0) {
        return fail("param '" + test.first + "." + kv.first + "' must be >= 1 ms");
      }
      if (is_time && kv.second > 30000.0) {
        return fail("param '" + test.first + "." + kv.first + "' exceeds 30 s safety limit");
      }
      const bool is_count = kv.first.find("_count") != std::string::npos ||
                            kv.first.find("_cycles") != std::string::npos ||
                            kv.first.find("_reps") != std::string::npos;
      if (is_count && kv.second > 10000.0) {
        return fail("param '" + test.first + "." + kv.first + "' exceeds 10000 count limit");
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
  if (!config_from_json(root, /*known_only=*/true, &config, nullptr, nullptr)) {
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
    for (const auto &test : requested.params) {
      auto base_test = base.params.find(test.first);
      if (base_test == base.params.end()) {
        continue;
      }
      for (const auto &kv : test.second) {
        if (base_test->second.count(kv.first) != 0) {
          base_test->second[kv.first] = kv.second;
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
