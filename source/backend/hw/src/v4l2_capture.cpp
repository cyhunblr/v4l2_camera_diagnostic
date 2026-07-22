#include "v4l2diag/hw/v4l2_capture.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace v4l2diag {

double V4lSession::ts_diff_ms(const struct timespec &end, const struct timespec &start) {
  return (static_cast<double>(end.tv_sec - start.tv_sec) * 1000.0) +
         (static_cast<double>(end.tv_nsec - start.tv_nsec) / 1'000'000.0);
}

void V4lSession::sleep_ns(uint64_t ns) {
  struct timespec req;
  req.tv_sec = static_cast<time_t>(ns / 1'000'000'000UL);
  req.tv_nsec = static_cast<long>(ns % 1'000'000'000UL);
  nanosleep(&req, nullptr);
}

void V4lSession::sleep_ms(int ms) {
  sleep_ns(static_cast<uint64_t>(ms) * 1'000'000UL);
}

bool V4lSession::open(const std::string &path, std::string *error) {
  fd_ = ::open(path.c_str(), O_RDWR | O_NONBLOCK);
  if (fd_ < 0) {
    if (error) {
      *error = "open(" + path + "): " + strerror(errno);
    }
    return false;
  }
  return true;
}

void V4lSession::cleanup() {
  release_buffers();
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
}

bool V4lSession::setup_buffers(int count, MemoryBackend backend, std::string *error) {
  memory_type_ = (backend == MemoryBackend::UserPtr) ? V4L2_MEMORY_USERPTR : V4L2_MEMORY_MMAP;

  struct v4l2_requestbuffers req;
  memset(&req, 0, sizeof(req));
  req.count = static_cast<unsigned>(count);
  req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  req.memory = memory_type_;

  if (ioctl(fd_, VIDIOC_REQBUFS, &req) < 0) {
    if (error) {
      *error = "VIDIOC_REQBUFS: " + std::string(strerror(errno));
    }
    return false;
  }

  buffers_.resize(req.count);
  for (auto &b : buffers_) {
    b = {nullptr, 0, -1, nullptr, nullptr};
  }

  if (memory_type_ == V4L2_MEMORY_USERPTR) {
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd_, VIDIOC_G_FMT, &fmt) < 0) {
      if (error) {
        *error = "VIDIOC_G_FMT: " + std::string(strerror(errno));
      }
      release_buffers();
      return false;
    }
    const size_t buf_size = fmt.fmt.pix.sizeimage;

    for (unsigned i = 0; i < req.count; i++) {
      void *mem = nullptr;
      if (posix_memalign(&mem, static_cast<size_t>(sysconf(_SC_PAGESIZE)), buf_size) != 0 || !mem) {
        if (error) {
          *error = "posix_memalign[" + std::to_string(i) + "]: allocation failed";
        }
        release_buffers();
        return false;
      }
      buffers_[i].user_start = mem;
      buffers_[i].length = buf_size;

      struct v4l2_buffer buf;
      memset(&buf, 0, sizeof(buf));
      buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      buf.memory = V4L2_MEMORY_USERPTR;
      buf.index = i;
      buf.m.userptr = reinterpret_cast<unsigned long>(mem);
      buf.length = static_cast<unsigned>(buf_size);

      if (ioctl(fd_, VIDIOC_QBUF, &buf) < 0) {
        if (error) {
          *error = "VIDIOC_QBUF[" + std::to_string(i) + "]: " + strerror(errno);
        }
        release_buffers();
        return false;
      }
    }

    return true;
  }

  for (unsigned i = 0; i < req.count; i++) {
    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = i;

    if (ioctl(fd_, VIDIOC_QUERYBUF, &buf) < 0) {
      if (error) {
        *error = "VIDIOC_QUERYBUF[" + std::to_string(i) + "]: " + strerror(errno);
      }
      release_buffers();
      return false;
    }

    buffers_[i].length = buf.length;
    buffers_[i].mmap_start = mmap(nullptr, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, buf.m.offset);
    if (buffers_[i].mmap_start == MAP_FAILED) {
      buffers_[i].mmap_start = nullptr;
      if (error) {
        *error = "mmap[" + std::to_string(i) + "]: " + strerror(errno);
      }
      release_buffers();
      return false;
    }

    if (ioctl(fd_, VIDIOC_QBUF, &buf) < 0) {
      if (error) {
        *error = "VIDIOC_QBUF[" + std::to_string(i) + "]: " + strerror(errno);
      }
      release_buffers();
      return false;
    }
  }

  if (backend == MemoryBackend::Dmabuf) {
    for (unsigned i = 0; i < buffers_.size(); i++) {
      struct v4l2_exportbuffer expbuf;
      memset(&expbuf, 0, sizeof(expbuf));
      expbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      expbuf.index = i;
      expbuf.flags = O_RDWR;

      if (ioctl(fd_, VIDIOC_EXPBUF, &expbuf) < 0) {
        if (error) {
          *error = "VIDIOC_EXPBUF[" + std::to_string(i) + "]: " + strerror(errno);
        }
        release_buffers();
        return false;
      }
      buffers_[i].dma_fd = expbuf.fd;

      void *dma = mmap(nullptr, buffers_[i].length, PROT_READ | PROT_WRITE, MAP_SHARED, expbuf.fd, 0);
      if (dma != MAP_FAILED) {
        buffers_[i].dma_start = dma;
      }
    }
  }

  return true;
}

void V4lSession::release_buffers() {
  enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (fd_ >= 0) {
    ioctl(fd_, VIDIOC_STREAMOFF, &type);
  }

  for (auto &b : buffers_) {
    if (b.dma_start && b.dma_start != MAP_FAILED) {
      munmap(b.dma_start, b.length);
    }
    if (b.dma_fd >= 0) {
      ::close(b.dma_fd);
    }
    if (b.mmap_start && b.mmap_start != MAP_FAILED) {
      munmap(b.mmap_start, b.length);
    }
    if (b.user_start) {
      free(b.user_start);
    }
  }
  buffers_.clear();

  if (fd_ >= 0) {
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = 0;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = memory_type_;
    ioctl(fd_, VIDIOC_REQBUFS, &req);
  }
  memory_type_ = V4L2_MEMORY_MMAP;
}

bool V4lSession::streamon(std::string *error) {
  enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (ioctl(fd_, VIDIOC_STREAMON, &type) < 0) {
    if (error) {
      *error = "VIDIOC_STREAMON: " + std::string(strerror(errno));
    }
    return false;
  }
  return true;
}

void V4lSession::streamoff() {
  enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  ioctl(fd_, VIDIOC_STREAMOFF, &type);
}

bool V4lSession::start(int count, MemoryBackend backend, std::string *error) {
  if (!setup_buffers(count, backend, error)) {
    return false;
  }
  if (!streamon(error)) {
    release_buffers();
    return false;
  }
  return true;
}

bool V4lSession::requeue_buffer(struct v4l2_buffer *buf) {
  buf->memory = memory_type_;
  if (memory_type_ == V4L2_MEMORY_USERPTR && buf->index < buffers_.size()) {
    const auto &b = buffers_[buf->index];
    buf->m.userptr = reinterpret_cast<unsigned long>(b.user_start);
    buf->length = static_cast<unsigned>(b.length);
  }
  return ioctl(fd_, VIDIOC_QBUF, buf) == 0;
}

int V4lSession::drain() {
  int drained = 0;
  while (true) {
    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = memory_type_;
    if (ioctl(fd_, VIDIOC_DQBUF, &buf) < 0) {
      break;
    }
    drained++;
    requeue_buffer(&buf);
  }
  return drained;
}

bool V4lSession::requeue(uint32_t index) {
  struct v4l2_buffer buf;
  memset(&buf, 0, sizeof(buf));
  buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  buf.index = index;
  return requeue_buffer(&buf);
}

CaptureFrame V4lSession::capture(TriggerSource &trigger, int poll_timeout_ms, bool do_drain, bool do_requeue) {
  CaptureFrame frame;

  if (do_drain) {
    drain();
    sleep_ms(10);
  }

  frame.t_trigger = trigger.send();

  struct pollfd pfd;
  pfd.fd = fd_;
  pfd.events = POLLIN;
  if (poll(&pfd, 1, poll_timeout_ms) <= 0 || !(pfd.revents & POLLIN)) {
    return frame;
  }

  struct v4l2_buffer buf;
  memset(&buf, 0, sizeof(buf));
  buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  buf.memory = memory_type_;

  if (ioctl(fd_, VIDIOC_DQBUF, &buf) == 0) {
    clock_gettime(CLOCK_REALTIME, &frame.t_recv);
    frame.success = true;
    frame.latency_ms = ts_diff_ms(frame.t_recv, frame.t_trigger);
    frame.index = buf.index;
    frame.sequence = buf.sequence;
    frame.flags = buf.flags;
    frame.bytesused = buf.bytesused;
    frame.timestamp = buf.timestamp;
    if (do_requeue) {
      requeue_buffer(&buf);
    }
  }

  return frame;
}

void V4lSession::warmup(TriggerSource &trigger, int count, int interval_ms, const std::atomic<bool> *cancel) {
  for (int i = 0; i < count; i++) {
    if (cancel && cancel->load()) {
      break;
    }
    trigger.send();
    sleep_ms(interval_ms);
    drain();
  }
}

}  // namespace v4l2diag
