#pragma once

#include <string>
#include <vector>

#include "v4l2diag/core/types.hpp"

namespace v4l2diag {

struct GpioMapping {
  int fsync_index = 0;
  int chip_id = 0;
  int line_number = 0;
  std::string description;
};

struct CameraMatcher {
  std::string driver;
  std::string card;
  std::string bus_info;
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
  std::vector<ReportFormat> report_formats;
};

struct CameraBinding {
  CameraMatcher camera;
  std::string trigger_channel_id;
};

struct DeviceProfile {
  int schema_version = 2;
  std::string id;
  std::string name;
  std::string description;
  bool enabled = true;
  CameraMatcher camera_match;
  ProfileDefaults defaults;
  std::vector<TriggerChannel> trigger_channels;
  std::vector<CameraBinding> camera_bindings;
};

class ProfileRegistry {
 public:
  explicit ProfileRegistry(std::string config_directory);

  std::vector<DeviceProfile> list_profiles() const;
  bool get_profile(const std::string &id, DeviceProfile *profile) const;
  bool add_or_update_profile(const DeviceProfile &profile, std::string *error);
  bool remove_profile(const std::string &id, std::string *error);

  const std::string &config_directory() const {
    return config_directory_;
  }

 private:
  std::string config_directory_;
  std::vector<DeviceProfile> profiles_;

  void load();
  void load_user_profiles();
  bool write_profile_file(const DeviceProfile &profile, std::string *error) const;
  std::string profile_path(const std::string &id) const;
};

std::string default_config_directory();
bool validate_device_profile(const DeviceProfile &profile, std::string *error);
}  // namespace v4l2diag
