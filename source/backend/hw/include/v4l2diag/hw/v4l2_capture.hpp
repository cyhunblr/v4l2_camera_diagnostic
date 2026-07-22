#pragma once

#include "v4l2diag/hw/trigger_source.hpp"
#include "v4l2diag/core/types.hpp"

#include <atomic>
#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

#include <linux/videodev2.h>

namespace v4l2diag {

struct BufferInfo {
  void *mmap_start = nullptr;
  size_t length = 0;
  int dma_fd = -1;
  void *dma_start = nullptr;
  void *user_start = nullptr;

  // The CPU-readable pointer for this buffer regardless of backend: MMAP and
  // DMABUF populate mmap_start, USERPTR populates user_start.
  void *data() const {
    return mmap_start ? mmap_start : user_start;
  }
};

struct CaptureFrame {
  bool success = false;
  double latency_ms = 0.0;
  uint32_t index = 0;
  uint32_t sequence = 0;
  uint32_t flags = 0;
  uint32_t bytesused = 0;
  struct timeval timestamp = {};
  struct timespec t_trigger = {};
  struct timespec t_recv = {};
};

/*
 * Manages a V4L2 device file descriptor together with its buffer set for a
 * single test invocation. The destructor ensures cleanup even on error paths
 * so that subsequent tests always start from a clean state.
 *
 * MMAP backend: buffers are kernel-allocated and mapped via mmap().
 * DMABUF backend: negotiated the same way as MMAP, then each buffer is
 * additionally exported via VIDIOC_EXPBUF so the caller can access the DMA
 * fd and the separately mapped dma_start pointer.
 * USERPTR backend: buffers are allocated in user space (page-aligned,
 * sized from VIDIOC_G_FMT) and handed to the driver via v4l2_buffer::m.userptr
 * on every VIDIOC_QBUF call — the kernel does not remember the pointer
 * across calls the way it does buffer index for MMAP.
 */
class V4lSession {
 public:
  V4lSession() = default;
  ~V4lSession() {
    cleanup();
  }

  V4lSession(const V4lSession &) = delete;
  V4lSession &operator=(const V4lSession &) = delete;

  bool open(const std::string &path, std::string *error = nullptr);
  void cleanup();
  bool is_open() const {
    return fd_ >= 0;
  }
  int fd() const {
    return fd_;
  }

  bool setup_buffers(int count, MemoryBackend backend, std::string *error = nullptr);
  void release_buffers();
  bool streamon(std::string *error = nullptr);
  void streamoff();

  // Convenience: setup_buffers + streamon.
  bool start(int count, MemoryBackend backend, std::string *error = nullptr);

  int drain();
  bool requeue(uint32_t index);
  CaptureFrame capture(TriggerSource &trigger, int poll_timeout_ms, bool do_drain = true, bool do_requeue = true);
  void warmup(TriggerSource &trigger, int count = 5, int interval_ms = 200, const std::atomic<bool> *cancel = nullptr);

  const std::vector<BufferInfo> &buffers() const {
    return buffers_;
  }
  size_t buffer_count() const {
    return buffers_.size();
  }

  static double ts_diff_ms(const struct timespec &end, const struct timespec &start);
  static void sleep_ms(int ms);
  static void sleep_ns(uint64_t ns);

 private:
  int fd_ = -1;
  std::vector<BufferInfo> buffers_;
  v4l2_memory memory_type_ = V4L2_MEMORY_MMAP;

  bool requeue_buffer(struct v4l2_buffer *buf);
};

}  // namespace v4l2diag
