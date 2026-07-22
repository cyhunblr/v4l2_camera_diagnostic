#pragma once

#include <map>
#include <string>
#include <vector>

namespace v4l2diag {

// Per-test threshold values, keyed by threshold name (e.g. "pass_delta_p95_ms").
// Every value is a double; integer-valued thresholds (counts) are stored as
// doubles too and compared as such by the runner.
using TestThresholds = std::map<std::string, double>;

// A named set of verdict thresholds. `values` maps a test id (e.g.
// "t22-latency-under-load") to that test's TestThresholds. Only tests that have
// tunable numeric verdict thresholds appear; structural/correctness checks
// (t01, t16, ...) are intentionally not represented here.
struct ThresholdConfig {
  std::string id;
  std::string name;
  std::string description;
  std::map<std::string, TestThresholds> values;

  // Returns values[test_id][key] if present, otherwise the built-in default for
  // that (test_id, key), otherwise 0.0. Never throws.
  double get(const std::string &test_id, const std::string &key) const;
};

// The built-in, always-available default configuration. Its values are exactly
// the thresholds that were previously hard-coded in the runner's verdict logic,
// so a run using "default" behaves identically to before this system existed.
ThresholdConfig default_threshold_config();

// Directory where user threshold configs (and a written-out copy of the
// default) live. Mirrors default_config_directory() but ends in /thresholds.
std::string default_threshold_directory();

class ThresholdRegistry {
 public:
  explicit ThresholdRegistry(std::string directory);

  std::vector<ThresholdConfig> list_configs() const;
  bool get_config(const std::string &id, ThresholdConfig *config) const;
  bool add_or_update_config(const ThresholdConfig &config, std::string *error);
  bool remove_config(const std::string &id, std::string *error);
  bool import_config(const std::string &json_text, std::string *error);
  bool export_config(const std::string &id, std::string *json_text) const;

  // Returns a fully-populated config for `id`: the built-in default with any
  // known values from config `id` overlaid on top. Unknown ids resolve to the
  // plain default. The result always has every test/key the runner needs.
  ThresholdConfig resolve(const std::string &id) const;

  const std::string &directory() const {
    return directory_;
  }

 private:
  std::string directory_;
  std::vector<ThresholdConfig> configs_;  // user configs only; "default" is built-in

  void load();
  void seed_default_file() const;
  bool write_config_file(const ThresholdConfig &config, std::string *error) const;
  std::string config_path(const std::string &id) const;
};

// Validation shared by add_or_update_config and import_config.
bool validate_threshold_config(const ThresholdConfig &config, std::string *error);

}  // namespace v4l2diag
