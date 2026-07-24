#pragma once

#include <map>
#include <string>
#include <vector>

namespace v4l2diag {

// Per-test key-value map. Used for both run parameters and verdict thresholds.
// Every value is a double; integer-valued entries (counts) are stored as doubles
// too and compared as such by the runner.
using TestThresholds = std::map<std::string, double>;

// A named test configuration combining run parameters (how a test executes) and
// verdict thresholds (what determines pass/warn/fail). Both map a test id to
// that test's key-value set.
struct ThresholdConfig {
  std::string id;
  std::string name;
  std::string description;
  // Verdict thresholds: pass/fail cut-off values per test.
  std::map<std::string, TestThresholds> values;
  // Run parameters: sample counts, timeouts, repetitions per test.
  std::map<std::string, TestThresholds> params;

  // Returns values[test_id][key] if present, otherwise the built-in default for
  // that (test_id, key), otherwise 0.0. Never throws.
  double get(const std::string &test_id, const std::string &key) const;

  // Returns params[test_id][key] if present, otherwise the built-in default for
  // that (test_id, key), otherwise 0.0. Never throws.
  double get_param(const std::string &test_id, const std::string &key) const;
};

// The built-in, always-available default configuration.
ThresholdConfig default_threshold_config();

// The built-in default run parameters.
std::map<std::string, TestThresholds> default_test_params();

// Directory where user configs live.
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

  // Returns a fully-populated config for `id`: the built-in defaults with any
  // known values from config `id` overlaid on top.
  ThresholdConfig resolve(const std::string &id) const;

  const std::string &directory() const {
    return directory_;
  }

 private:
  std::string directory_;
  std::vector<ThresholdConfig> configs_;

  void load();
  void seed_default_file() const;
  bool write_config_file(const ThresholdConfig &config, std::string *error) const;
  std::string config_path(const std::string &id) const;
};

// Validation shared by add_or_update_config and import_config.
bool validate_threshold_config(const ThresholdConfig &config, std::string *error);

}  // namespace v4l2diag
