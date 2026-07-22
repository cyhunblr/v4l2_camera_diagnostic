#include "v4l2diag/hw/device_discovery.hpp"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sstream>
#include <string>
#include <sys/ioctl.h>
#include <unistd.h>
#include <vector>

namespace v4l2diag {

namespace {

template <typename T>
void clear_struct(T *value) {
  std::memset(value, 0, sizeof(T));
}

bool starts_with_video(const std::string &name) {
  if (name.rfind("video", 0) != 0 || name.size() <= 5) {
    return false;
  }
  return std::all_of(name.begin() + 5, name.end(), [](unsigned char c) { return std::isdigit(c); });
}

int video_index(const std::string &path) {
  const std::size_t pos = path.find_last_not_of("0123456789");
  if (pos == std::string::npos || pos + 1 >= path.size()) {
    return -1;
  }
  return std::atoi(path.c_str() + pos + 1);
}

void enumerate_formats(int fd, uint32_t type, const std::string &type_name, std::vector<V4L2FormatInfo> *formats) {
  for (uint32_t index = 0; index < 128; ++index) {
    v4l2_fmtdesc desc;
    clear_struct(&desc);
    desc.index = index;
    desc.type = type;
    if (ioctl(fd, VIDIOC_ENUM_FMT, &desc) < 0) {
      break;
    }
    V4L2FormatInfo info;
    info.fourcc = fourcc_to_string(desc.pixelformat);
    info.description = reinterpret_cast<const char *>(desc.description);
    info.buffer_type = type_name;
    formats->push_back(info);
  }
}

bool probe_requestbuffers(int fd, uint32_t memory, std::string *detail) {
  v4l2_requestbuffers req;
  clear_struct(&req);
  req.count = 1;
  req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  req.memory = memory;
  if (ioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
    *detail = std::strerror(errno);
    return false;
  }

  clear_struct(&req);
  req.count = 0;
  req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  req.memory = memory;
  ioctl(fd, VIDIOC_REQBUFS, &req);
  *detail = "VIDIOC_REQBUFS accepted";
  return true;
}

bool release_mmap_buffers(int fd) {
  v4l2_requestbuffers req;
  clear_struct(&req);
  req.count = 0;
  req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  req.memory = V4L2_MEMORY_MMAP;
  return ioctl(fd, VIDIOC_REQBUFS, &req) == 0;
}

bool probe_dmabuf_export(int fd, std::string *detail) {
  v4l2_requestbuffers req;
  clear_struct(&req);
  req.count = 1;
  req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  req.memory = V4L2_MEMORY_MMAP;
  if (ioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
    *detail = std::string("MMAP REQBUFS failed before EXPBUF: ") + std::strerror(errno);
    return false;
  }

  v4l2_buffer buf;
  clear_struct(&buf);
  buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  buf.memory = V4L2_MEMORY_MMAP;
  buf.index = 0;
  if (ioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) {
    *detail = std::string("VIDIOC_QUERYBUF failed before EXPBUF: ") + std::strerror(errno);
    release_mmap_buffers(fd);
    return false;
  }

  v4l2_exportbuffer expbuf;
  clear_struct(&expbuf);
  expbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  expbuf.index = 0;
  expbuf.flags = O_RDWR;
  if (ioctl(fd, VIDIOC_EXPBUF, &expbuf) < 0) {
    *detail = std::string("VIDIOC_EXPBUF failed: ") + std::strerror(errno);
    release_mmap_buffers(fd);
    return false;
  }

  close(expbuf.fd);
  release_mmap_buffers(fd);
  *detail = "VIDIOC_EXPBUF accepted for MMAP buffer export";
  return true;
}

}  // namespace

std::string fourcc_to_string(uint32_t fourcc) {
  char chars[5];
  chars[0] = static_cast<char>(fourcc & 0xFF);
  chars[1] = static_cast<char>((fourcc >> 8) & 0xFF);
  chars[2] = static_cast<char>((fourcc >> 16) & 0xFF);
  chars[3] = static_cast<char>((fourcc >> 24) & 0xFF);
  chars[4] = '\0';
  return chars;
}

std::vector<DeviceInfo> discover_video_devices(const std::string &dev_root) {
  std::vector<std::string> paths;
  DIR *dir = opendir(dev_root.c_str());
  if (!dir) {
    return {};
  }

  while (dirent *entry = readdir(dir)) {
    const std::string name = entry->d_name;
    if (starts_with_video(name)) {
      paths.push_back(dev_root + "/" + name);
    }
  }
  closedir(dir);

  std::sort(paths.begin(), paths.end(), [](const std::string &a, const std::string &b) {
    const int ai = video_index(a);
    const int bi = video_index(b);
    if (ai != bi) {
      return ai < bi;
    }
    return a < b;
  });

  std::vector<DeviceInfo> devices;
  for (const auto &path : paths) {
    DeviceInfo info;
    query_device(path, &info);
    devices.push_back(info);
  }
  return devices;
}

bool query_device(const std::string &path, DeviceInfo *info) {
  *info = DeviceInfo{};
  info->path = path;

  const int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK);
  if (fd < 0) {
    info->error = std::strerror(errno);
    return false;
  }

  v4l2_capability cap;
  clear_struct(&cap);
  if (ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
    info->error = std::strerror(errno);
    close(fd);
    return false;
  }

  info->readable = true;
  info->driver = reinterpret_cast<const char *>(cap.driver);
  info->card = reinterpret_cast<const char *>(cap.card);
  info->bus_info = reinterpret_cast<const char *>(cap.bus_info);
  info->capabilities = cap.capabilities;
  info->device_caps = cap.device_caps;
  const uint32_t effective_caps = cap.device_caps != 0 ? cap.device_caps : cap.capabilities;
  info->supports_capture =
      (effective_caps & V4L2_CAP_VIDEO_CAPTURE) || (effective_caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE);
  info->supports_streaming = (effective_caps & V4L2_CAP_STREAMING) != 0;

  if (effective_caps & V4L2_CAP_VIDEO_CAPTURE) {
    enumerate_formats(fd, V4L2_BUF_TYPE_VIDEO_CAPTURE, "single-plane", &info->formats);
  }
  if (effective_caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE) {
    enumerate_formats(fd, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, "multi-plane", &info->formats);
  }

  close(fd);
  return true;
}

std::vector<MemoryBackendProbe> probe_memory_backends(const std::string &path) {
  std::vector<MemoryBackendProbe> probes;
  const int fd = open(path.c_str(), O_RDWR | O_NONBLOCK);
  if (fd < 0) {
    const std::string detail = std::strerror(errno);
    probes.push_back({MemoryBackend::Mmap, false, detail});
    probes.push_back({MemoryBackend::Dmabuf, false, detail});
    probes.push_back({MemoryBackend::UserPtr, false, detail});
    return probes;
  }

  std::string mmap_detail;
  probes.push_back({MemoryBackend::Mmap, probe_requestbuffers(fd, V4L2_MEMORY_MMAP, &mmap_detail), mmap_detail});

  std::string dmabuf_detail;
  probes.push_back({MemoryBackend::Dmabuf, probe_dmabuf_export(fd, &dmabuf_detail), dmabuf_detail});

  std::string userptr_detail;
  probes.push_back(
      {MemoryBackend::UserPtr, probe_requestbuffers(fd, V4L2_MEMORY_USERPTR, &userptr_detail), userptr_detail});

  close(fd);
  return probes;
}

}  // namespace v4l2diag
