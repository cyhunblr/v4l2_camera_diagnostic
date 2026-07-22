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

}  // namespace v4l2diag
