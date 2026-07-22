#pragma once

#include "v4l2diag/core/profile_registry.hpp"
#include "v4l2diag/hw/trigger_source.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace v4l2diag {

struct V4l2ControlMenuItem {
  int64_t value = 0;
  std::string name;
};

struct V4l2ControlInfo {
  uint32_t id = 0;
  uint32_t type = 0;
  std::string name;
  int64_t minimum = 0;
  int64_t maximum = 0;
  uint64_t step = 0;
  int64_t default_value = 0;
  int64_t current_value = 0;
  uint32_t flags = 0;
  bool readable = false;
  bool writable = false;
  bool supported_for_trigger = false;
  std::vector<V4l2ControlMenuItem> menu_items;
};

struct ControlDeviceInfo {
  std::string path;
  std::string kind;
  std::string driver;
  std::string card;
  std::string bus_info;
  std::string sysfs_name;
  std::string error;
  std::vector<V4l2ControlInfo> controls;
};

std::vector<ControlDeviceInfo> discover_control_devices(const std::string &dev_root = "/dev");
bool query_control_device(const std::string &path, ControlDeviceInfo *info);
bool resolve_control_device(const ControlDeviceSelector &selector, const std::string &capture_path,
                            std::string *resolved_path, std::string *error);
bool validate_v4l2_control_write(const V4l2ControlInfo &control, const V4l2ControlWrite &write, std::string *error);

class V4l2ControlIo {
 public:
  virtual ~V4l2ControlIo() = default;
  virtual int open_device(const std::string &path, std::string *error) = 0;
  virtual void close_device(int fd) = 0;
  virtual bool query_control(int fd, uint32_t id, V4l2ControlInfo *control, std::string *error) = 0;
  virtual bool set_control(int fd, const V4l2ControlInfo &control, int64_t value, std::string *error) = 0;
};

class V4l2ControlTrigger final : public TriggerSource {
 public:
  explicit V4l2ControlTrigger(std::shared_ptr<V4l2ControlIo> io = {});
  ~V4l2ControlTrigger() override;
  bool open(const std::string &capture_path, const TriggerChannel &channel, std::string *error = nullptr);
  bool is_open() const {
    return fd_ >= 0;
  }
  TriggerMode mode() const override {
    return TriggerMode::Software;
  }
  struct timespec send(uint64_t pulse_ns = 13'000'000UL) override;
  const std::string &last_error() const {
    return last_error_;
  }

 private:
  bool apply(const std::vector<V4l2ControlWrite> &writes, std::string *error);

  int fd_ = -1;
  std::shared_ptr<V4l2ControlIo> io_;
  std::vector<V4l2ControlWrite> teardown_controls_;
  std::vector<V4l2ControlWrite> fire_controls_;
  std::string last_error_;
};

}  // namespace v4l2diag
