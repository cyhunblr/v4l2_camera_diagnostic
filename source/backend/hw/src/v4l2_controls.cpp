#include "v4l2diag/hw/v4l2_controls.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <linux/videodev2.h>
#include <memory>
#include <sstream>
#include <string>
#include <sys/ioctl.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace v4l2diag {

namespace {

template <typename T>
void clear_struct(T *value) {
  std::memset(value, 0, sizeof(T));
}

std::string basename(const std::string &path) {
  const std::size_t slash = path.find_last_of('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string read_line(const std::string &path) {
  std::ifstream in(path);
  std::string value;
  std::getline(in, value);
  return value;
}

bool supported_scalar_type(uint32_t type) {
  return type == V4L2_CTRL_TYPE_INTEGER || type == V4L2_CTRL_TYPE_BOOLEAN || type == V4L2_CTRL_TYPE_MENU ||
         type == V4L2_CTRL_TYPE_INTEGER_MENU || type == V4L2_CTRL_TYPE_BITMASK || type == V4L2_CTRL_TYPE_BUTTON ||
         type == V4L2_CTRL_TYPE_INTEGER64;
}

bool matches(const std::string &expected, const std::string &actual) {
  return expected.empty() || expected == actual;
}

bool query_current_value(int fd, const v4l2_query_ext_ctrl &query, int64_t *value) {
  if (!supported_scalar_type(query.type) || query.type == V4L2_CTRL_TYPE_BUTTON) {
    return false;
  }
  v4l2_ext_control control;
  clear_struct(&control);
  control.id = query.id;
  v4l2_ext_controls controls;
  clear_struct(&controls);
  controls.ctrl_class = V4L2_CTRL_ID2CLASS(query.id);
  controls.count = 1;
  controls.controls = &control;
  if (ioctl(fd, VIDIOC_G_EXT_CTRLS, &controls) != 0) {
    return false;
  }
  *value = query.type == V4L2_CTRL_TYPE_INTEGER64 ? control.value64 : control.value;
  return true;
}

class LinuxV4l2ControlIo final : public V4l2ControlIo {
 public:
  int open_device(const std::string &path, std::string *error) override {
    const int fd = ::open(path.c_str(), O_RDWR | O_NONBLOCK);
    if (fd < 0 && error) {
      *error = std::string("failed to open control device: ") + std::strerror(errno);
    }
    return fd;
  }

  void close_device(int fd) override {
    close(fd);
  }

  bool query_control(int fd, uint32_t id, V4l2ControlInfo *control, std::string *error) override {
    v4l2_query_ext_ctrl query;
    clear_struct(&query);
    query.id = id;
    if (ioctl(fd, VIDIOC_QUERY_EXT_CTRL, &query) != 0) {
      if (error) {
        *error = "V4L2 control " + std::to_string(id) + " is unavailable";
      }
      return false;
    }
    control->id = query.id;
    control->type = query.type;
    control->name = query.name;
    control->minimum = query.minimum;
    control->maximum = query.maximum;
    control->step = query.step;
    control->default_value = query.default_value;
    control->flags = query.flags;
    control->readable = (query.flags & V4L2_CTRL_FLAG_WRITE_ONLY) == 0 && query.type != V4L2_CTRL_TYPE_BUTTON;
    control->writable =
        (query.flags & (V4L2_CTRL_FLAG_DISABLED | V4L2_CTRL_FLAG_READ_ONLY | V4L2_CTRL_FLAG_INACTIVE)) == 0;
    control->supported_for_trigger = control->writable && supported_scalar_type(query.type);
    query_current_value(fd, query, &control->current_value);
    return true;
  }

  bool set_control(int fd, const V4l2ControlInfo &control_info, int64_t value, std::string *error) override {
    v4l2_ext_control control;
    clear_struct(&control);
    control.id = control_info.id;
    if (control_info.type == V4L2_CTRL_TYPE_INTEGER64) {
      control.value64 = value;
    } else {
      control.value = static_cast<int32_t>(value);
    }
    v4l2_ext_controls controls;
    clear_struct(&controls);
    controls.ctrl_class = V4L2_CTRL_ID2CLASS(control_info.id);
    controls.count = 1;
    controls.controls = &control;
    if (ioctl(fd, VIDIOC_S_EXT_CTRLS, &controls) != 0) {
      if (error) {
        *error = "failed to set V4L2 control '" + control_info.name + "': " + std::strerror(errno);
      }
      return false;
    }
    return true;
  }
};

}  // namespace

bool query_control_device(const std::string &path, ControlDeviceInfo *info) {
  *info = ControlDeviceInfo{};
  info->path = path;
  const std::string node = basename(path);
  info->kind = node.rfind("v4l-subdev", 0) == 0 ? "subdevice" : "video";
  info->sysfs_name = read_line("/sys/class/video4linux/" + node + "/name");

  const int fd = open(path.c_str(), O_RDWR | O_NONBLOCK);
  if (fd < 0) {
    info->error = std::strerror(errno);
    return false;
  }

  v4l2_capability capability;
  clear_struct(&capability);
  if (ioctl(fd, VIDIOC_QUERYCAP, &capability) == 0) {
    info->driver = reinterpret_cast<const char *>(capability.driver);
    info->card = reinterpret_cast<const char *>(capability.card);
    info->bus_info = reinterpret_cast<const char *>(capability.bus_info);
  } else if (info->kind == "subdevice") {
    info->card = info->sysfs_name;
  }

  v4l2_query_ext_ctrl query;
  clear_struct(&query);
  uint32_t next_flags = V4L2_CTRL_FLAG_NEXT_CTRL | V4L2_CTRL_FLAG_NEXT_COMPOUND;
  query.id = next_flags;
  int query_result = ioctl(fd, VIDIOC_QUERY_EXT_CTRL, &query);
  if (query_result != 0 && errno == EINVAL) {
    clear_struct(&query);
    next_flags = V4L2_CTRL_FLAG_NEXT_CTRL;
    query.id = next_flags;
    query_result = ioctl(fd, VIDIOC_QUERY_EXT_CTRL, &query);
  }
  while (query_result == 0) {
    V4l2ControlInfo control;
    control.id = query.id;
    control.type = query.type;
    control.name = query.name;
    control.minimum = query.minimum;
    control.maximum = query.maximum;
    control.step = query.step;
    control.default_value = query.default_value;
    control.flags = query.flags;
    control.readable = (query.flags & V4L2_CTRL_FLAG_WRITE_ONLY) == 0 && query.type != V4L2_CTRL_TYPE_BUTTON;
    control.writable =
        (query.flags & (V4L2_CTRL_FLAG_DISABLED | V4L2_CTRL_FLAG_READ_ONLY | V4L2_CTRL_FLAG_INACTIVE)) == 0;
    control.supported_for_trigger = control.writable && supported_scalar_type(query.type);
    query_current_value(fd, query, &control.current_value);

    if (query.type == V4L2_CTRL_TYPE_MENU || query.type == V4L2_CTRL_TYPE_INTEGER_MENU) {
      for (int64_t index = query.minimum; index <= query.maximum; ++index) {
        v4l2_querymenu menu;
        clear_struct(&menu);
        menu.id = query.id;
        menu.index = static_cast<uint32_t>(index);
        if (ioctl(fd, VIDIOC_QUERYMENU, &menu) == 0) {
          V4l2ControlMenuItem item;
          item.value = query.type == V4L2_CTRL_TYPE_INTEGER_MENU ? menu.value : index;
          item.name = query.type == V4L2_CTRL_TYPE_MENU ? reinterpret_cast<const char *>(menu.name)
                                                        : std::to_string(menu.value);
          control.menu_items.push_back(std::move(item));
        }
      }
    }
    info->controls.push_back(std::move(control));
    query.id |= next_flags;
    query_result = ioctl(fd, VIDIOC_QUERY_EXT_CTRL, &query);
  }
  close(fd);
  return true;
}

std::vector<ControlDeviceInfo> discover_control_devices(const std::string &dev_root) {
  std::vector<std::string> paths;
  DIR *dir = opendir(dev_root.c_str());
  if (!dir) {
    return {};
  }
  while (dirent *entry = readdir(dir)) {
    const std::string name = entry->d_name;
    if (name.rfind("video", 0) == 0 || name.rfind("v4l-subdev", 0) == 0) {
      paths.push_back(dev_root + "/" + name);
    }
  }
  closedir(dir);
  std::sort(paths.begin(), paths.end());

  std::vector<ControlDeviceInfo> devices;
  for (const auto &path : paths) {
    ControlDeviceInfo info;
    query_control_device(path, &info);
    devices.push_back(std::move(info));
  }
  return devices;
}

bool resolve_control_device(const ControlDeviceSelector &selector, const std::string &capture_path,
                            std::string *resolved_path, std::string *error) {
  if (selector.kind == ControlDeviceSelector::Kind::CaptureDevice) {
    *resolved_path = capture_path;
    return true;
  }
  const std::string expected_kind = selector.kind == ControlDeviceSelector::Kind::SubDevice ? "subdevice" : "video";
  std::vector<std::string> matches_found;
  for (const auto &device : discover_control_devices()) {
    if (device.kind == expected_kind && matches(selector.driver, device.driver) &&
        matches(selector.card, device.card) && matches(selector.bus_info, device.bus_info) &&
        matches(selector.sysfs_name, device.sysfs_name)) {
      matches_found.push_back(device.path);
    }
  }
  if (matches_found.size() != 1) {
    if (error) {
      *error = matches_found.empty() ? "control device selector did not match a node"
                                     : "control device selector matched more than one node";
    }
    return false;
  }
  *resolved_path = matches_found.front();
  return true;
}

bool validate_v4l2_control_write(const V4l2ControlInfo &control, const V4l2ControlWrite &write, std::string *error) {
  if ((!write.name.empty() && write.name != control.name) || (write.type != 0 && write.type != control.type)) {
    if (error) {
      *error = "V4L2 control metadata changed for id " + std::to_string(write.id);
    }
    return false;
  }
  if (!control.supported_for_trigger) {
    if (error) {
      *error = "V4L2 control is not writable or has an unsupported type: " + control.name;
    }
    return false;
  }
  if (control.type != V4L2_CTRL_TYPE_BUTTON &&
      (write.value < control.minimum || write.value > control.maximum ||
       (control.step > 1 && (write.value - control.minimum) % static_cast<int64_t>(control.step) != 0))) {
    if (error) {
      *error = "V4L2 control value is outside the advertised range: " + control.name;
    }
    return false;
  }
  return true;
}

V4l2ControlTrigger::V4l2ControlTrigger(std::shared_ptr<V4l2ControlIo> io)
    : io_(io ? std::move(io) : std::make_shared<LinuxV4l2ControlIo>()) {}

V4l2ControlTrigger::~V4l2ControlTrigger() {
  if (fd_ >= 0) {
    std::string ignored;
    apply(teardown_controls_, &ignored);
    io_->close_device(fd_);
  }
}

bool V4l2ControlTrigger::open(const std::string &capture_path, const TriggerChannel &channel, std::string *error) {
  std::string path;
  if (!resolve_control_device(channel.control_device, capture_path, &path, error)) {
    return false;
  }
  fd_ = io_->open_device(path, error);
  if (fd_ < 0) {
    return false;
  }
  fire_controls_ = channel.fire_controls;
  teardown_controls_ = channel.teardown_controls;
  if (!apply(channel.setup_controls, error)) {
    io_->close_device(fd_);
    fd_ = -1;
    return false;
  }
  return true;
}

bool V4l2ControlTrigger::apply(const std::vector<V4l2ControlWrite> &writes, std::string *error) {
  for (const auto &write : writes) {
    V4l2ControlInfo control;
    if (!io_->query_control(fd_, write.id, &control, error) || !validate_v4l2_control_write(control, write, error) ||
        !io_->set_control(fd_, control, write.value, error)) {
      return false;
    }
  }
  return true;
}

struct timespec V4l2ControlTrigger::send(uint64_t) {
  struct timespec issued_at {};
  clock_gettime(CLOCK_REALTIME, &issued_at);
  last_error_.clear();
  apply(fire_controls_, &last_error_);
  return issued_at;
}

}  // namespace v4l2diag
