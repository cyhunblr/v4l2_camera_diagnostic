#pragma once

#include "v4l2diag/core/types.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace v4l2diag {

struct V4L2FormatInfo {
  std::string fourcc;
  std::string description;
  std::string buffer_type;
};

struct DeviceInfo {
  std::string path;
  std::string driver;
  std::string card;
  std::string bus_info;
  uint32_t capabilities = 0;
  uint32_t device_caps = 0;
  bool readable = false;
  bool supports_capture = false;
  bool supports_streaming = false;
  std::string error;
  std::vector<V4L2FormatInfo> formats;
};

struct MemoryBackendProbe {
  MemoryBackend backend = MemoryBackend::Mmap;
  bool supported = false;
  std::string detail;
};

std::string fourcc_to_string(uint32_t fourcc);
std::vector<DeviceInfo> discover_video_devices(const std::string &dev_root = "/dev");
bool query_device(const std::string &path, DeviceInfo *info);
std::vector<MemoryBackendProbe> probe_memory_backends(const std::string &path);

// Append `candidate` unless an entry with the same fourcc AND buffer type is already listed.
//
// A driver may advertise one fourcc at several VIDIOC_ENUM_FMT indices: the tegra isx021
// reports UYVY at index 0 and again at index 2, both single-plane. Listing it twice made T01
// publish "3 formats" while T17, which skipped repeats when building its own list, measured
// 2 on the same device.
//
// The key includes the buffer type on purpose -- the same fourcc under single-plane and
// under multi-plane is two real capture configurations, and query_device() enumerates both
// types into one vector.
//
// Exposed (rather than left inside the enumeration loop) so the rule is testable without a
// V4L2 device: the loop around it needs a real ioctl, this decision does not.
bool append_distinct_format(const V4L2FormatInfo &candidate, std::vector<V4L2FormatInfo> *formats);

}  // namespace v4l2diag
