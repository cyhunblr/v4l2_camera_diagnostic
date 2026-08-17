#pragma once

#include <utility>

#include <string>
#include <vector>

#include "v4l2diag/core/config_version.hpp"
#include "v4l2diag/core/role_bindings.hpp"
#include "v4l2diag/core/types.hpp"

namespace v4l2diag {

struct GpioMapping {
  int fsync_index = 0;
  int chip_id = 0;
  int line_number = 0;
  std::string description;
};

struct ControlDeviceSelector {
  enum class Kind {
    CaptureDevice,
    VideoDevice,
    SubDevice,
  };

  Kind kind = Kind::CaptureDevice;
  std::string driver;
  std::string card;
  std::string bus_info;
  std::string sysfs_name;
};

struct V4l2ControlWrite {
  uint32_t id = 0;
  std::string name;
  uint32_t type = 0;
  int64_t value = 0;
};

struct TriggerChannel {
  enum class Type {
    Hardware,
    Software,
  };

  std::string id;
  std::string name;
  std::string description;
  Type type = Type::Hardware;
  GpioMapping gpio;
  ControlDeviceSelector control_device;
  std::vector<V4l2ControlWrite> setup_controls;
  std::vector<V4l2ControlWrite> fire_controls;
  std::vector<V4l2ControlWrite> teardown_controls;
};

struct ProfileDefaults {
  TriggerMode trigger_mode = TriggerMode::FreeRun;
  std::vector<MemoryBackend> memory_backends;
  std::vector<std::string> test_selectors;
  // No report_formats. v5 removed it: the user does not choose formats, every run
  // writes HTML, JSON and Markdown. ReportFormat itself lives on as the identity of
  // an emitted artifact (see ReportArtifact) -- what went is the preference.
  double trigger_rate_hz = 30.0;
  double pulse_width_ms = 13.0;
};

struct DeviceProfile {
  // Single source of truth: a v4 bump must not require finding this line too.
  int schema_version = kProfileSchemaVersion;
  std::string id;
  std::string name;
  std::string description;
  // No `enabled`. v3 removed it: a profile is either present or deleted, and the
  // flag let one exist while being invisible with nothing in the file saying
  // which state was intended.
  //
  // No `camera_match` and no matcher-based `camera_bindings` either. v4 removed
  // both: a Trigger Profile does not identify a physical camera. On this hardware
  // a {driver, card, bus_info} matcher cannot tell four video nodes apart, so
  // routing goes through run roles instead -- see role_bindings.hpp.
  ProfileDefaults defaults;
  std::vector<TriggerChannel> trigger_channels;
  // role -> trigger channel, validated against the run topology before a run
  // starts. Empty is legitimate for a free-run profile.
  std::vector<RoleBinding> role_bindings;
};

class ProfileRegistry {
 public:
  explicit ProfileRegistry(std::string config_directory);

  std::vector<DeviceProfile> list_profiles() const;
  // One entry per config file found in the config directory, including ones too
  // new or too broken to load. `profiles()` above lists only runnable configs;
  // this is what the UI needs to explain the rest and to prefill a migration
  // form. A Legacy config that parsed cleanly carries `draft`.
  struct StoredConfig {
    std::string file;
    // Profile id read from the file. Empty when the file could not be parsed.
    std::string profile_id;
    MigrationReport report;
    // Values migrated in memory, for prefilling the migration form. Only
    // meaningful when report.has_draft(); never added to the runnable list.
    DeviceProfile draft;
  };
  std::vector<StoredConfig> stored_configs() const;
  bool get_profile(const std::string &id, DeviceProfile *profile) const;
  bool add_or_update_profile(const DeviceProfile &profile, std::string *error);

  // Commits a migration against a specific source file rather than against a
  // profile id.
  //
  // add_or_update_profile() always writes `<profile.id>.json`, so saving a
  // migrated draft whose file name differs from its id -- or whose id the user
  // changed on the migration form -- created a second file and left the original
  // in place, still reported as "migration required" forever. This replaces the
  // source file instead: it writes the new profile, then removes the old file
  // when the target path differs.
  //
  // `source_file` must be the `file` of an entry in stored_configs(); anything
  // else is rejected, which also means a path can never be traversed out of the
  // config directory.
  //
  // The source must also be a config that genuinely needs repairing -- it has a
  // draft AND needs user input. That excludes Future and Malformed files (neither
  // parses) and a runnable Current one, so this endpoint cannot be used to rewrite
  // or delete a config that was never up for migration.
  //
  // Collisions are checked against every stored config, not just the runnable
  // ones: a Future/Legacy/Malformed file is invisible to get_profile() yet would
  // still be overwritten. The target-path check applies only when the write lands
  // somewhere else; the profile-id check applies always, because even an in-place
  // migration can promote a config to an id another file already defines.
  //
  // Writing one file is atomic; replacing a *different* file is two steps, so a
  // failure to remove the source attempts a rollback of the new target; when that
  // succeeds the source is left as the only config for this profile. If the
  // rollback also fails, *error says so explicitly and names both paths.
  //
  // Returns false with *error set on any failure.
  bool migrate_config(const std::string &source_file, const DeviceProfile &profile, std::string *error);
  bool remove_profile(const std::string &id, std::string *error);

  const std::string &config_directory() const {
    return config_directory_;
  }

 private:
  std::string config_directory_;
  std::vector<DeviceProfile> profiles_;

  void load();
  std::vector<StoredConfig> stored_configs_;
  void load_user_profiles();
  bool write_profile_file(const DeviceProfile &profile, std::string *error) const;
  std::string profile_path(const std::string &id) const;
};

std::string default_config_directory();
bool validate_device_profile(const DeviceProfile &profile, std::string *error);
}  // namespace v4l2diag
