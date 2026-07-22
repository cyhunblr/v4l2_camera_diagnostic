#include "v4l2diag/core/diagnostic_runner.hpp"

#include "v4l2diag/hw/gpio_trigger.hpp"
#include "v4l2diag/hw/trigger_source.hpp"
#include "v4l2diag/hw/v4l2_controls.hpp"
#include "v4l2diag/hw/v4l2_capture.hpp"
#include "v4l2diag/hw/device_discovery.hpp"
#include "v4l2diag/core/stats.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <future>
#include <linux/videodev2.h>
#include <memory>
#include <map>
#include <numeric>
#include <poll.h>
#include <set>
#include <sstream>
#include <string>
#include <sys/ioctl.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

#ifdef __has_include
#if __has_include(<linux/dma-buf.h>)
#include <linux/dma-buf.h>
#define V4L2DIAG_HAS_DMA_BUF_SYNC 1
#endif
#endif
#ifndef V4L2DIAG_HAS_DMA_BUF_SYNC
#define V4L2DIAG_HAS_DMA_BUF_SYNC 0
#endif

namespace v4l2diag {

namespace {

// Log callback helper — emits a fine-grained log line if the callback is set.
using LogFn = std::function<void(const std::string &severity, const std::string &camera, const std::string &test,
                                 const std::string &message, const std::string &log_type)>;

static void emit(const LogFn &log, const std::string &camera, const std::string &test, const std::string &msg,
                 const std::string &severity = "info", const std::string &log_type = "progress") {
  if (log)
    log(severity, camera, test, msg, log_type);
}

// Convenience wrappers for structured log types.
static void emit_section(const LogFn &log, const std::string &camera, const std::string &test, const std::string &msg) {
  emit(log, camera, test, msg, "info", "section_start");
}

static void emit_data(const LogFn &log, const std::string &camera, const std::string &test, const std::string &msg) {
  emit(log, camera, test, msg, "info", "data");
}

// Format a stats block as a multi-line string matching legacy output style.
static std::string format_stats(const std::string &label, const Stats &s) {
  char buf[512];
  snprintf(buf, sizeof(buf),
           "%s: n=%zu mean=%.3f stddev=%.3f median=%.3f\n"
           "  min=%.3f max=%.3f p5=%.3f p95=%.3f p99=%.3f\n"
           "  jitter=%.3f outliers=%zu",
           label.c_str(), s.count, s.mean, s.stddev, s.median, s.min, s.max, s.p5, s.p95, s.p99, s.jitter, s.outliers);
  return buf;
}

// Format a histogram as a multi-line ASCII bar chart.
static std::string format_histogram(const std::string &label, const std::vector<double> &data, int bins = 10) {
  if (data.empty())
    return "";
  std::vector<double> sorted = data;
  std::sort(sorted.begin(), sorted.end());
  double lo = sorted.front(), hi = sorted.back();
  if (hi - lo < 0.001)
    hi = lo + 1.0;
  double bin_width = (hi - lo) / bins;

  std::vector<int> counts(bins, 0);
  for (double v : data) {
    int idx = std::min(static_cast<int>((v - lo) / bin_width), bins - 1);
    counts[idx]++;
  }
  int max_count = *std::max_element(counts.begin(), counts.end());

  std::string result = label + " (" + std::to_string(bins) + " bins):\n";
  for (int i = 0; i < bins; i++) {
    char line[128];
    double bin_lo = lo + i * bin_width;
    double bin_hi = bin_lo + bin_width;
    int bar_len = max_count > 0 ? (counts[i] * 30 / max_count) : 0;
    snprintf(line, sizeof(line), "  [%6.1f-%6.1f] %3d |", bin_lo, bin_hi, counts[i]);
    result += line;
    for (int j = 0; j < bar_len; j++)
      result += "\xe2\x96\x88";  // █
    for (int j = bar_len; j < 30; j++)
      result += "\xe2\x96\x91";  // ░
    result += "|\n";
  }
  return result;
}

double elapsed_ms(std::chrono::steady_clock::time_point start, std::chrono::steady_clock::time_point end) {
  return std::chrono::duration<double, std::milli>(end - start).count();
}

MetricValue metric(const std::string &name, const std::string &unit, double value, const std::string &description) {
  return {name, unit, value, description};
}

void push_stats_metrics(std::vector<MetricValue> &metrics, const std::string &prefix, const Stats &s,
                        const std::string &unit = "ms") {
  metrics.push_back(metric(prefix + "_mean", unit, s.mean, "Mean " + prefix));
  metrics.push_back(metric(prefix + "_stddev", unit, s.stddev, "Std-dev " + prefix));
  metrics.push_back(metric(prefix + "_min", unit, s.min, "Min " + prefix));
  metrics.push_back(metric(prefix + "_max", unit, s.max, "Max " + prefix));
  metrics.push_back(metric(prefix + "_p95", unit, s.p95, "95th-percentile " + prefix));
  metrics.push_back(metric(prefix + "_jitter", unit, s.jitter, "Jitter " + prefix));
}

// Docs: docs/backend/tests/t01-no-streamon.md
void run_t01(const std::string &camera_path, MemoryBackend backend, TestResult &r, const LogFn &log) {
  emit(log, camera_path, "t01", "Opening device for STREAMON state check...");
  V4lSession s;
  std::string err;
  if (!s.open(camera_path, &err)) {
    r.status = TestStatus::Error;
    r.summary = "Failed to open device: " + err;
    return;
  }
  if (!s.setup_buffers(2, backend, &err)) {
    r.status = TestStatus::Error;
    r.summary = "VIDIOC_REQBUFS failed: " + err;
    return;
  }

  emit(log, camera_path, "t01", "Buffers allocated. Testing poll(50ms) and DQBUF without STREAMON...");

  struct pollfd pfd;
  pfd.fd = s.fd();
  pfd.events = POLLIN;
  const int poll_ret = poll(&pfd, 1, 50);

  struct v4l2_buffer buf;
  memset(&buf, 0, sizeof(buf));
  buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  buf.memory = V4L2_MEMORY_MMAP;
  const int dq_ret = ioctl(s.fd(), VIDIOC_DQBUF, &buf);
  const int dq_errno = errno;

  r.metrics.push_back(metric("poll_returned", "count", static_cast<double>(poll_ret),
                             "poll(50ms) return value without STREAMON; expected 0."));
  r.metrics.push_back(
      metric("dqbuf_failed", "bool", dq_ret < 0 ? 1.0 : 0.0, "Whether DQBUF correctly failed without STREAMON."));
  r.metrics.push_back(metric("dqbuf_errno", "errno", static_cast<double>(dq_errno),
                             "errno from DQBUF without STREAMON; expected EAGAIN(11) or EINVAL(22)."));

  r.details.push_back("poll(50ms) returned " + std::to_string(poll_ret) + " (expected 0)");
  if (dq_ret < 0) {
    r.details.push_back("DQBUF returned -1, errno=" + std::to_string(dq_errno) + " (" + strerror(dq_errno) + ")");
  } else {
    r.details.push_back("DQBUF unexpectedly succeeded, sequence=" + std::to_string(buf.sequence));
  }

  const bool poll_ok = (poll_ret == 0);
  const bool dq_ok = (dq_ret < 0) && (dq_errno == EAGAIN || dq_errno == EINVAL);

  if (poll_ok && dq_ok) {
    r.status = TestStatus::Pass;
    r.summary = "No frames delivered before VIDIOC_STREAMON. V4L2 state machine correct.";
  } else if (!dq_ok && dq_ret >= 0) {
    r.status = TestStatus::Fail;
    r.summary = "Frame delivered without VIDIOC_STREAMON — V4L2 state machine violation.";
  } else {
    r.status = TestStatus::Warn;
    r.summary = "DQBUF correctly fails, but poll returned non-zero without STREAMON.";
  }
}

// Docs: docs/backend/tests/t02-buffer-overwrite.md
void run_t02(const std::string &camera_path, MemoryBackend backend, TriggerSource &trigger, TestResult &r,
             const LogFn &log, const TestThresholds &th) {
  struct Variant {
    int triggers;
    int interval_ms;
    char key;
    const char *label;
  };
  static const Variant variants[] = {
      {100, 100, 'A', "Variant A: 100 triggers @ 100ms"},
      {200, 50, 'B', "Variant B: 200 triggers @ 50ms"},
  };

  int error_frames_total = 0;
  for (const auto &v : variants) {
    emit(log, camera_path, "t02", std::string(v.label) + ": sending " + std::to_string(v.triggers) + " triggers...");
    V4lSession s;
    std::string err;
    if (!s.open(camera_path, &err) || !s.start(2, backend, &err)) {
      r.status = TestStatus::Error;
      r.summary = "Session setup failed: " + err;
      return;
    }
    V4lSession::sleep_ms(500);
    s.drain();

    for (int i = 0; i < v.triggers; i++) {
      trigger.send();
      V4lSession::sleep_ms(v.interval_ms - 13);
    }

    int available = 0;
    int error_frames = 0;
    while (true) {
      struct v4l2_buffer buf;
      memset(&buf, 0, sizeof(buf));
      buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      buf.memory = V4L2_MEMORY_MMAP;
      if (ioctl(s.fd(), VIDIOC_DQBUF, &buf) < 0)
        break;
      available++;
      if (buf.flags & V4L2_BUF_FLAG_ERROR)
        error_frames++;
    }
    error_frames_total += error_frames;

    const std::string k(1, v.key);
    r.metrics.push_back(metric("triggers_" + k, "count", static_cast<double>(v.triggers), v.label));
    r.metrics.push_back(metric("frames_available_" + k, "count", static_cast<double>(available),
                               "Frames available after all triggers in variant " + k + "."));
    r.details.push_back(std::string(v.label) + ": buffers=2 triggers=" + std::to_string(v.triggers) +
                        " available=" + std::to_string(available) +
                        (error_frames > 0 ? " errors=" + std::to_string(error_frames) : ""));
  }

  r.metrics.push_back(metric("error_flag_total", "count", static_cast<double>(error_frames_total),
                             "Frames with V4L2_BUF_FLAG_ERROR across both variants."));
  const int max_err = static_cast<int>(th.count("max_error_flags") ? th.at("max_error_flags") : 0);
  if (error_frames_total > max_err) {
    r.status = TestStatus::Warn;
    r.summary =
        "Buffer saturation test completed; " + std::to_string(error_frames_total) + " frames had V4L2_BUF_FLAG_ERROR.";
  } else {
    r.status = TestStatus::Pass;
    r.summary = "Buffer saturation test completed. No error flags.";
  }
}

// Docs: docs/backend/tests/t03-gpio-latency.md
void run_t03(const std::string &camera_path, MemoryBackend backend, TriggerSource &trigger, TestResult &r,
             const LogFn &log) {
  constexpr int SAMPLES = 50;
  V4lSession s;
  std::string err;
  if (!s.open(camera_path, &err) || !s.start(2, backend, &err)) {
    r.status = TestStatus::Error;
    r.summary = "Session setup failed: " + err;
    return;
  }
  emit(log, camera_path, "t03", "Warming up camera (5 triggers)...");
  s.warmup(trigger);

  emit(log, camera_path, "t03", "Capturing " + std::to_string(SAMPLES) + " latency samples @ 200ms interval...");
  std::vector<double> latencies;
  latencies.reserve(SAMPLES);
  int misses = 0;
  for (int i = 0; i < SAMPLES; i++) {
    auto f = s.capture(trigger, 100);
    if (f.success) {
      latencies.push_back(f.latency_ms);
      if ((i + 1) % 10 == 0 || i == SAMPLES - 1) {
        const Stats partial = compute_stats(latencies);
        emit(log, camera_path, "t03",
             "  Sample " + std::to_string(i + 1) + "/" + std::to_string(SAMPLES) +
                 ": lat=" + std::to_string(static_cast<int>(f.latency_ms)) + "ms" +
                 " (running mean=" + std::to_string(static_cast<int>(partial.mean)) +
                 " stddev=" + std::to_string(static_cast<int>(partial.stddev)) + "ms)");
      }
    } else {
      misses++;
      emit(log, camera_path, "t03", "  Sample " + std::to_string(i + 1) + "/" + std::to_string(SAMPLES) + ": MISS",
           "warn");
    }
    V4lSession::sleep_ms(200);
  }

  r.metrics.push_back(
      metric("frames_captured", "count", static_cast<double>(latencies.size()), "Frames successfully captured."));
  r.metrics.push_back(metric("frames_missed", "count", static_cast<double>(misses), "Frames missed."));

  if (latencies.empty()) {
    r.status = TestStatus::Fail;
    r.summary = "All " + std::to_string(SAMPLES) + " capture attempts timed out.";
    return;
  }
  const Stats sl = compute_stats(latencies);
  push_stats_metrics(r.metrics, "latency", sl);
  emit_data(log, camera_path, "t03",
            format_stats("GPIO\xe2\x86\x92"
                         "DQBUF latency (ms)",
                         sl));
  emit_data(log, camera_path, "t03", format_histogram("Latency distribution", latencies, 10));
  r.status = TestStatus::Pass;
  r.summary = "Captured " + std::to_string(latencies.size()) + "/" + std::to_string(SAMPLES) +
              " frames. mean=" + std::to_string(static_cast<int>(sl.mean)) +
              "ms p95=" + std::to_string(static_cast<int>(sl.p95)) + "ms.";
}

// Docs: docs/backend/tests/t04-nonblock-vs-block.md
void run_t04(const std::string &camera_path, MemoryBackend backend, TriggerSource &trigger, TestResult &r,
             const LogFn &log) {
  constexpr int SAMPLES = 30;
  emit(log, camera_path, "t04", "Running NON_BLOCK vs BLOCK comparison (30 samples each)...");

  std::vector<double> nb_lat;
  double avg_spins = 0.0;
  {
    V4lSession s;
    std::string err;
    if (!s.open(camera_path, &err) || !s.start(2, backend, &err)) {
      r.status = TestStatus::Error;
      r.summary = "NON_BLOCK session failed: " + err;
      return;
    }
    s.warmup(trigger);
    std::vector<int> spins;
    for (int i = 0; i < SAMPLES; i++) {
      s.drain();
      V4lSession::sleep_ms(10);
      struct timespec t_trig = trigger.send();
      struct timespec deadline;
      clock_gettime(CLOCK_REALTIME, &deadline);
      deadline.tv_nsec += 100'000'000L;
      if (deadline.tv_nsec >= 1'000'000'000L) {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1'000'000'000L;
      }
      int spin = 0;
      while (true) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        if (ioctl(s.fd(), VIDIOC_DQBUF, &buf) == 0) {
          struct timespec t_recv;
          clock_gettime(CLOCK_REALTIME, &t_recv);
          nb_lat.push_back(V4lSession::ts_diff_ms(t_recv, t_trig));
          spins.push_back(spin);
          ioctl(s.fd(), VIDIOC_QBUF, &buf);
          break;
        }
        if (errno != EAGAIN)
          break;
        spin++;
        struct timespec now;
        clock_gettime(CLOCK_REALTIME, &now);
        if (now.tv_sec > deadline.tv_sec || (now.tv_sec == deadline.tv_sec && now.tv_nsec >= deadline.tv_nsec))
          break;
      }
      V4lSession::sleep_ms(200);
    }
    if (!spins.empty())
      avg_spins = std::accumulate(spins.begin(), spins.end(), 0.0) / spins.size();
  }

  std::vector<double> bl_lat;
  {
    V4lSession s;
    std::string err;
    if (!s.open(camera_path, &err)) {
      r.status = TestStatus::Error;
      r.summary = "BLOCK session open failed: " + err;
      return;
    }
    int flags = fcntl(s.fd(), F_GETFL);
    fcntl(s.fd(), F_SETFL, flags & ~O_NONBLOCK);
    if (!s.start(2, backend, &err)) {
      r.status = TestStatus::Error;
      r.summary = "BLOCK session start failed: " + err;
      return;
    }
    for (int i = 0; i < 5; i++) {
      trigger.send();
      struct pollfd pfd = {s.fd(), POLLIN, 0};
      if (poll(&pfd, 1, 200) > 0) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        if (ioctl(s.fd(), VIDIOC_DQBUF, &buf) == 0)
          ioctl(s.fd(), VIDIOC_QBUF, &buf);
      }
    }
    for (int i = 0; i < SAMPLES; i++) {
      while (true) {
        struct pollfd pfd = {s.fd(), POLLIN, 0};
        if (poll(&pfd, 1, 0) <= 0)
          break;
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        if (ioctl(s.fd(), VIDIOC_DQBUF, &buf) == 0)
          ioctl(s.fd(), VIDIOC_QBUF, &buf);
        else
          break;
      }
      V4lSession::sleep_ms(10);
      struct timespec t_trig = trigger.send();
      struct v4l2_buffer buf;
      memset(&buf, 0, sizeof(buf));
      buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      buf.memory = V4L2_MEMORY_MMAP;
      if (ioctl(s.fd(), VIDIOC_DQBUF, &buf) == 0) {
        struct timespec t_recv;
        clock_gettime(CLOCK_REALTIME, &t_recv);
        bl_lat.push_back(V4lSession::ts_diff_ms(t_recv, t_trig));
        ioctl(s.fd(), VIDIOC_QBUF, &buf);
      }
      V4lSession::sleep_ms(200);
    }
  }

  if (!nb_lat.empty()) {
    push_stats_metrics(r.metrics, "nonblock_latency", compute_stats(nb_lat));
    r.metrics.push_back(metric("avg_eagain_spins", "count", avg_spins, "Avg EAGAIN spins per frame."));
    r.metrics.push_back(
        metric("nonblock_captures", "count", static_cast<double>(nb_lat.size()), "NON_BLOCK captures."));
  }
  if (!bl_lat.empty()) {
    push_stats_metrics(r.metrics, "block_latency", compute_stats(bl_lat));
    r.metrics.push_back(metric("block_captures", "count", static_cast<double>(bl_lat.size()), "BLOCK captures."));
  }

  if (nb_lat.empty() && bl_lat.empty()) {
    r.status = TestStatus::Fail;
    r.summary = "No frames captured in either mode.";
    return;
  }
  r.status = TestStatus::Pass;
  r.summary = "NON_BLOCK " + std::to_string(nb_lat.size()) + "/" + std::to_string(SAMPLES) + " OK; BLOCK " +
              std::to_string(bl_lat.size()) + "/" + std::to_string(SAMPLES) + " OK.";
}

// Docs: docs/backend/tests/t05-poll-timeout-effect.md
void run_t05(const std::string &camera_path, MemoryBackend backend, TriggerSource &trigger, TestResult &r,
             const LogFn &log) {
  static const int timeouts_ms[] = {1, 2, 5, 10, 20, 30, 40, 45, 48, 50, 55, 60, 100, 500};
  constexpr int N = static_cast<int>(sizeof(timeouts_ms) / sizeof(timeouts_ms[0]));
  constexpr int SAMPLES = 20;
  emit(log, camera_path, "t05", "Poll timeout effect: testing 14 timeout values x 20 samples...");

  V4lSession s;
  std::string err;
  if (!s.open(camera_path, &err) || !s.start(2, backend, &err)) {
    r.status = TestStatus::Error;
    r.summary = "Session setup failed: " + err;
    return;
  }
  s.warmup(trigger);

  int cliff_ms = -1, last_clean_ms = -1;
  for (int t = 0; t < N; t++) {
    const int tms = timeouts_ms[t];
    std::vector<double> lat;
    int misses = 0;
    for (int i = 0; i < SAMPLES; i++) {
      auto f = s.capture(trigger, tms);
      if (f.success)
        lat.push_back(f.latency_ms);
      else
        misses++;
      V4lSession::sleep_ms(200);
    }
    if (misses == 0)
      last_clean_ms = tms;
    if (misses > 0 && cliff_ms < 0)
      cliff_ms = tms;

    std::ostringstream d;
    d << "timeout=" << tms << "ms: ";
    if (!lat.empty()) {
      const Stats st = compute_stats(lat);
      d << "mean=" << st.mean << " miss=" << misses << "/" << SAMPLES;
    } else {
      d << "ALL MISSED";
    }
    r.details.push_back(d.str());
    emit(log, camera_path, "t05", "  " + d.str());
    if (misses == SAMPLES)
      break;
  }

  r.metrics.push_back(
      metric("cliff_timeout_ms", "ms", static_cast<double>(cliff_ms), "First timeout with misses (-1 = none)."));
  r.metrics.push_back(
      metric("last_clean_timeout_ms", "ms", static_cast<double>(last_clean_ms), "Highest timeout with zero misses."));

  if (last_clean_ms < 0) {
    r.status = TestStatus::Fail;
    r.summary = "Misses at all timeout values.";
  } else if (cliff_ms < 0) {
    r.status = TestStatus::Pass;
    r.summary = "No misses at any timeout value.";
  } else {
    r.status = TestStatus::Pass;
    r.summary = "Cliff at " + std::to_string(cliff_ms) + "ms; last clean=" + std::to_string(last_clean_ms) + "ms.";
  }
}

// Docs: docs/backend/tests/t06-format-comparison.md
void run_t06(const std::string &camera_path, MemoryBackend backend, TriggerSource &trigger, TestResult &r,
             const LogFn &log) {
  constexpr int SAMPLES = 20;
  static const char *fmts[] = {"YUYV", "UYVY"};
  emit(log, camera_path, "t06", "Format comparison: YUYV vs UYVY (20 samples each)...");

  int ctrl_fd = ::open(camera_path.c_str(), O_RDWR | O_NONBLOCK);
  if (ctrl_fd < 0) {
    r.status = TestStatus::Error;
    r.summary = "Cannot open device: " + std::string(strerror(errno));
    return;
  }
  struct v4l2_format orig;
  memset(&orig, 0, sizeof(orig));
  orig.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  const bool has_orig = (ioctl(ctrl_fd, VIDIOC_G_FMT, &orig) == 0);

  for (int fi = 0; fi < 2; fi++) {
    const char *fn = fmts[fi];
    const uint32_t fourcc = static_cast<uint32_t>(fn[0]) | (static_cast<uint32_t>(fn[1]) << 8) |
                            (static_cast<uint32_t>(fn[2]) << 16) | (static_cast<uint32_t>(fn[3]) << 24);
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = 1920;
    fmt.fmt.pix.height = 1280;
    fmt.fmt.pix.pixelformat = fourcc;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;
    if (ioctl(ctrl_fd, VIDIOC_S_FMT, &fmt) < 0) {
      r.details.push_back(std::string(fn) + ": S_FMT failed");
      continue;
    }
    r.details.push_back(std::string(fn) + ": sizeimage=" + std::to_string(fmt.fmt.pix.sizeimage));

    V4lSession s;
    std::string err;
    s.open(camera_path, &err);
    ioctl(s.fd(), VIDIOC_S_FMT, &fmt);
    if (!s.start(2, backend, &err)) {
      r.details.push_back(std::string(fn) + ": start failed: " + err);
      continue;
    }
    s.warmup(trigger);

    std::vector<double> lat;
    double mbps = 0.0;
    for (int i = 0; i < SAMPLES; i++) {
      auto f = s.capture(trigger, 100);
      if (f.success) {
        lat.push_back(f.latency_ms);
        if (lat.size() == 1 && f.index < s.buffer_count()) {
          const void *src = s.buffers()[f.index].data();
          size_t sz = fmt.fmt.pix.sizeimage;
          if (src && sz > 0) {
            std::vector<uint8_t> dst(sz);
            struct timespec t0, t1;
            clock_gettime(CLOCK_REALTIME, &t0);
            for (int rep = 0; rep < 50; rep++)
              memcpy(dst.data(), src, sz);
            clock_gettime(CLOCK_REALTIME, &t1);
            const double el = V4lSession::ts_diff_ms(t1, t0) / 1000.0;
            mbps = (static_cast<double>(sz) * 50.0 / 1048576.0) / el;
          }
        }
      }
      V4lSession::sleep_ms(200);
    }
    if (!lat.empty()) {
      std::string prefix = std::string(fn);
      std::transform(prefix.begin(), prefix.end(), prefix.begin(), ::tolower);
      push_stats_metrics(r.metrics, prefix + "_latency", compute_stats(lat));
      r.metrics.push_back(metric(prefix + "_throughput_mbps", "MB/s", mbps, std::string(fn) + " memcpy throughput."));
    }
  }

  if (has_orig)
    ioctl(ctrl_fd, VIDIOC_S_FMT, &orig);
  ::close(ctrl_fd);
  r.status = TestStatus::Pass;
  r.summary = "Format comparison complete. See metrics for per-format latency and throughput.";
}

// Docs: docs/backend/tests/t07-poll-timeout-sweep.md
void run_t07(const std::string &camera_path, MemoryBackend backend, TriggerSource &trigger, TestResult &r,
             const LogFn &log, const TestThresholds &th) {
  constexpr int FRAMES = 10;
  const double PROD_MS = th.count("production_timeout_ms") ? th.at("production_timeout_ms") : 48.5;
  emit(log, camera_path, "t07", "Poll timeout sweep: 200ms → 1ms, 10 frames per level...");

  V4lSession s;
  std::string err;
  if (!s.open(camera_path, &err) || !s.start(2, backend, &err)) {
    r.status = TestStatus::Error;
    r.summary = "Session setup failed: " + err;
    return;
  }
  s.warmup(trigger, 10, 200);

  std::vector<int> timeouts;
  for (int t = 200; t >= 10; t -= 10)
    timeouts.push_back(t);
  for (int t = 9; t >= 1; t--)
    timeouts.push_back(t);

  int last_clean = -1, first_miss = -1, last_alive = -1, first_dead = -1;

  for (int tms : timeouts) {
    std::vector<double> lat;
    int misses = 0;
    for (int i = 0; i < FRAMES; i++) {
      auto f = s.capture(trigger, tms);
      if (f.success)
        lat.push_back(f.latency_ms);
      else
        misses++;
      V4lSession::sleep_ms(200);
    }
    const int hits = FRAMES - misses;
    if (misses == 0)
      last_clean = tms;
    if (misses > 0 && first_miss < 0)
      first_miss = tms;
    if (hits > 0)
      last_alive = tms;
    if (misses == FRAMES && first_dead < 0)
      first_dead = tms;

    std::ostringstream d;
    d << "timeout=" << tms << "ms hits=" << hits << "/" << FRAMES;
    if (!lat.empty())
      d << " mean=" << static_cast<int>(compute_stats(lat).mean) << "ms";
    r.details.push_back(d.str());
    const char *sym = (misses == 0) ? "✓" : (misses == FRAMES) ? "✖" : "⚠";
    // Visual bar: each █ = 1 hit out of FRAMES
    std::string bar;
    for (int b = 0; b < FRAMES; b++)
      bar += (b < hits) ? "█" : "░";
    char line_buf[128];
    snprintf(line_buf, sizeof(line_buf), "  %3dms %s %s %2d/%d", tms, bar.c_str(), sym, hits, FRAMES);
    emit(log, camera_path, "t07", std::string(line_buf));
  }

  const double safety = PROD_MS - static_cast<double>(last_clean);

  // Emit a box-drawn sweep summary
  {
    auto fmt_int = [](int v) {
      char buf[16];
      snprintf(buf, sizeof(buf), "%dms", v);
      return std::string(buf);
    };
    auto fmt_dbl = [](double v) {
      char buf[16];
      snprintf(buf, sizeof(buf), "%.1fms", v);
      return std::string(buf);
    };
    char r0[64], r1[64], r2[64], r3[64];
    snprintf(r0, sizeof(r0), "║  Production timeout : %7s  ║", fmt_dbl(PROD_MS).c_str());
    snprintf(r1, sizeof(r1), "║  Lowest 0%% miss     : %7s  ║", fmt_int(last_clean).c_str());
    snprintf(r2, sizeof(r2), "║  First miss at      : %7s  ║", fmt_int(first_miss).c_str());
    snprintf(r3, sizeof(r3), "║  Safety margin      : %7s  ║", fmt_dbl(safety).c_str());
    std::string box;
    box += "╔═══════════ SWEEP SUMMARY ═══════╗\n";
    box += std::string(r0) + "\n";
    box += std::string(r1) + "\n";
    box += std::string(r2) + "\n";
    box += std::string(r3) + "\n";
    box += "╚═════════════════════════════════╝";
    emit_data(log, camera_path, "t07", box);
  }

  r.metrics.push_back(metric("last_clean_ms", "ms", static_cast<double>(last_clean), "Lowest 0% miss timeout."));
  r.metrics.push_back(metric("first_miss_ms", "ms", static_cast<double>(first_miss), "First timeout with misses."));
  r.metrics.push_back(metric("last_alive_ms", "ms", static_cast<double>(last_alive), "Lowest timeout with any hit."));
  r.metrics.push_back(metric("first_dead_ms", "ms", static_cast<double>(first_dead), "First 100% miss timeout."));
  r.metrics.push_back(metric("safety_margin_ms", "ms", safety, "Production timeout - last clean timeout."));

  if (safety >= (th.count("safe_margin_ms") ? th.at("safe_margin_ms") : 5.0)) {
    r.status = TestStatus::Pass;
    r.summary = "Safe. Cliff at " + std::to_string(first_miss) + "ms; " + std::to_string(static_cast<int>(safety)) +
                "ms margin above production timeout.";
  } else if (safety >= 0.0) {
    r.status = TestStatus::Warn;
    r.summary = "Tight. Cliff at " + std::to_string(first_miss) + "ms; only " + std::to_string(safety) + "ms margin.";
  } else {
    r.status = TestStatus::Fail;
    r.summary = "Cliff (" + std::to_string(first_miss) + "ms) above production timeout (" +
                std::to_string(static_cast<int>(PROD_MS)) + "ms).";
  }
}

// Docs: docs/backend/tests/t08-sequence-continuity.md
void run_t08(const std::string &camera_path, MemoryBackend backend, TriggerSource &trigger, TestResult &r,
             const LogFn &log, const TestThresholds &th) {
  constexpr int NUM = 100;
  emit(log, camera_path, "t08", "Sequence continuity: capturing 100 frames...");
  V4lSession s;
  std::string err;
  if (!s.open(camera_path, &err) || !s.start(2, backend, &err)) {
    r.status = TestStatus::Error;
    r.summary = "Session setup failed: " + err;
    return;
  }
  s.warmup(trigger);

  std::vector<uint32_t> seqs;
  std::vector<double> ts_us;
  seqs.reserve(NUM);
  ts_us.reserve(NUM);
  for (int i = 0; i < NUM; i++) {
    auto f = s.capture(trigger, 100);
    if (f.success) {
      seqs.push_back(f.sequence);
      ts_us.push_back(f.timestamp.tv_sec * 1e6 + f.timestamp.tv_usec);
    }
    V4lSession::sleep_ms(100);
  }

  if (seqs.size() < 2) {
    r.status = TestStatus::Error;
    r.summary = "Insufficient frames.";
    return;
  }

  int total_gaps = 0, max_gap = 0, duplicates = 0, ts_non_mono = 0;
  for (size_t i = 1; i < seqs.size(); i++) {
    const int gap = static_cast<int>(seqs[i]) - static_cast<int>(seqs[i - 1]);
    if (gap == 0)
      duplicates++;
    else if (gap > 1) {
      total_gaps += (gap - 1);
      max_gap = std::max(max_gap, gap - 1);
    }
    if (ts_us[i] <= ts_us[i - 1])
      ts_non_mono++;
  }

  r.metrics.push_back(metric("frames_captured", "count", static_cast<double>(seqs.size()), "Frames captured."));
  r.metrics.push_back(metric("dropped_frames", "count", static_cast<double>(total_gaps), "Sequence gaps."));
  r.metrics.push_back(metric("max_gap", "count", static_cast<double>(max_gap), "Largest single gap."));
  r.metrics.push_back(metric("duplicates", "count", static_cast<double>(duplicates), "Duplicate sequences."));
  r.metrics.push_back(
      metric("ts_non_monotonic", "count", static_cast<double>(ts_non_mono), "Non-monotonic timestamps."));
  r.details.push_back("Sequence range: " + std::to_string(seqs.front()) + " → " + std::to_string(seqs.back()));

  if (total_gaps == 0 && duplicates == 0 && ts_non_mono == 0) {
    r.status = TestStatus::Pass;
    r.summary = "All " + std::to_string(seqs.size()) + " frames continuous; no anomalies.";
  } else {
    const int max_drop = static_cast<int>(th.count("max_dropped_frames") ? th.at("max_dropped_frames") : 5);
    const int max_nm = static_cast<int>(th.count("max_non_monotonic") ? th.at("max_non_monotonic") : 0);
    r.status = (total_gaps > max_drop || ts_non_mono > max_nm) ? TestStatus::Fail : TestStatus::Warn;
    r.summary = "dropped=" + std::to_string(total_gaps) + " dup=" + std::to_string(duplicates) +
                " non_mono=" + std::to_string(ts_non_mono) + ".";
  }
}

// Docs: docs/backend/tests/t09-sustained-capture.md
void run_t09(const std::string &camera_path, MemoryBackend backend, TriggerSource &trigger, TestResult &r,
             const LogFn &log, const TestThresholds &th) {
  constexpr int DURATION_SEC = 60;
  constexpr int WINDOW_SEC = 10;
  constexpr int INTERVAL_MS = 100;
  emit(log, camera_path, "t09", "Sustained capture: 60 seconds @ 10Hz (6 windows of 10s)...");

  V4lSession s;
  std::string err;
  if (!s.open(camera_path, &err) || !s.start(2, backend, &err)) {
    r.status = TestStatus::Error;
    r.summary = "Session setup failed: " + err;
    return;
  }
  s.warmup(trigger);

  const auto t_start = std::chrono::steady_clock::now();
  std::vector<double> all_lat;
  int total_misses = 0, max_cons_miss = 0, cons_miss = 0;
  int cur_win = 0;
  std::vector<double> win_lat;
  int win_miss = 0;
  std::vector<double> win_means;

  while (true) {
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - t_start).count();
    if (elapsed >= DURATION_SEC)
      break;
    const int win = static_cast<int>(elapsed / WINDOW_SEC);
    if (win != cur_win && !win_lat.empty()) {
      const Stats ws = compute_stats(win_lat);
      win_means.push_back(ws.mean);
      std::string win_msg = "Win" + std::to_string(cur_win) + " " + std::to_string(cur_win * WINDOW_SEC) + "-" +
                            std::to_string((cur_win + 1) * WINDOW_SEC) + "s: n=" + std::to_string(win_lat.size()) +
                            " mean=" + std::to_string(static_cast<int>(ws.mean)) +
                            "ms stddev=" + std::to_string(static_cast<int>(ws.stddev)) +
                            " miss=" + std::to_string(win_miss);
      r.details.push_back(win_msg);
      emit(log, camera_path, "t09", "  " + win_msg);
      win_lat.clear();
      win_miss = 0;
      cur_win = win;
    }
    auto f = s.capture(trigger, 100);
    if (f.success) {
      all_lat.push_back(f.latency_ms);
      win_lat.push_back(f.latency_ms);
      cons_miss = 0;
    } else {
      total_misses++;
      win_miss++;
      cons_miss++;
      max_cons_miss = std::max(max_cons_miss, cons_miss);
    }
    V4lSession::sleep_ms(INTERVAL_MS);
  }

  const int total = static_cast<int>(all_lat.size()) + total_misses;
  const double rate = total > 0 ? 100.0 * static_cast<double>(all_lat.size()) / total : 0.0;
  r.metrics.push_back(metric("frames_captured", "count", static_cast<double>(all_lat.size()), "Total frames."));
  r.metrics.push_back(metric("success_rate_pct", "%", rate, "Success rate."));
  r.metrics.push_back(
      metric("max_consecutive_miss", "count", static_cast<double>(max_cons_miss), "Max consecutive miss."));

  double drift = 0.0;
  if (!all_lat.empty()) {
    push_stats_metrics(r.metrics, "latency", compute_stats(all_lat));
    if (win_means.size() >= 2) {
      const size_t half = win_means.size() / 2;
      double f = 0.0, sec = 0.0;
      for (size_t i = 0; i < half; i++)
        f += win_means[i];
      for (size_t i = half; i < win_means.size(); i++)
        sec += win_means[i];
      drift = (sec / (win_means.size() - half)) - (f / half);
    }
  }
  r.metrics.push_back(metric("latency_drift_ms", "ms", drift, "Second half mean - first half mean."));

  const double pass_rate = th.count("pass_rate_pct") ? th.at("pass_rate_pct") : 95.0;
  const double warn_rate = th.count("warn_rate_pct") ? th.at("warn_rate_pct") : 80.0;
  const double pass_drift = th.count("pass_drift_ms") ? th.at("pass_drift_ms") : 1.0;
  const double warn_drift = th.count("warn_drift_ms") ? th.at("warn_drift_ms") : 5.0;
  if (rate >= pass_rate && std::abs(drift) < pass_drift)
    r.status = TestStatus::Pass;
  else if (rate >= warn_rate && std::abs(drift) < warn_drift)
    r.status = TestStatus::Warn;
  else
    r.status = TestStatus::Fail;
  r.summary = std::to_string(static_cast<int>(all_lat.size())) + "/" + std::to_string(total) + " frames (" +
              std::to_string(static_cast<int>(rate)) + "%) drift=" + std::to_string(drift) + "ms.";
}

// Docs: docs/backend/tests/t10-multi-buffer.md
void run_t10(const std::string &camera_path, MemoryBackend backend, TriggerSource *trigger, TestResult &r,
             const LogFn &log) {
  static const int counts[] = {1, 2, 3, 4, 5};
  constexpr int SAMPLES = 20;
  emit(log, camera_path, "t10", "Multi-buffer configurations: testing 1-5 buffers...");

  for (int bc : counts) {
    V4lSession s;
    std::string err;
    if (!s.open(camera_path, &err)) {
      r.details.push_back("count=" + std::to_string(bc) + ": open failed");
      continue;
    }

    // Probe REQBUFS grant count
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = static_cast<unsigned>(bc);
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (ioctl(s.fd(), VIDIOC_REQBUFS, &req) < 0) {
      r.details.push_back("count=" + std::to_string(bc) + ": REQBUFS failed");
      continue;
    }
    const int granted = static_cast<int>(req.count);
    r.metrics.push_back(metric("granted_for_" + std::to_string(bc), "count", static_cast<double>(granted),
                               "Buffers granted when " + std::to_string(bc) + " requested."));
    req.count = 0;
    ioctl(s.fd(), VIDIOC_REQBUFS, &req);  // release

    if (trigger) {
      if (!s.start(bc, backend, &err)) {
        r.details.push_back("count=" + std::to_string(bc) + ": start failed");
        continue;
      }
      s.warmup(*trigger, 3, 200);
      std::vector<double> lat;
      int misses = 0;
      for (int i = 0; i < SAMPLES; i++) {
        auto f = s.capture(*trigger, 100);
        if (f.success)
          lat.push_back(f.latency_ms);
        else
          misses++;
        V4lSession::sleep_ms(200);
      }
      std::ostringstream d10;
      d10 << "count=" << bc << " granted=" << granted;
      if (!lat.empty())
        d10 << " mean=" << static_cast<int>(compute_stats(lat).mean) << "ms miss=" << misses << "/" << SAMPLES;
      r.details.push_back(d10.str());
    } else {
      r.details.push_back("count=" + std::to_string(bc) + " granted=" + std::to_string(granted) +
                          " (GPIO unavailable, capture skipped)");
    }
  }

  r.status = TestStatus::Pass;
  r.summary = "Buffer count probe complete. See details for granted counts" +
              std::string(trigger ? " and capture latency." : ".");
}

// Docs: docs/backend/tests/t11-buffer-recycling.md
void run_t11(const std::string &camera_path, MemoryBackend backend, TriggerSource &trigger, TestResult &r,
             const LogFn &log, const TestThresholds &th) {
  static const int delays_ms[] = {0, 1, 5, 10, 20, 30, 40, 48, 50, 60, 80, 100};
  constexpr int N = static_cast<int>(sizeof(delays_ms) / sizeof(delays_ms[0]));
  constexpr int REPS = 10;
  emit(log, camera_path, "t11", "Buffer recycling: testing 12 delay values x 10 reps...");

  V4lSession s;
  std::string err;
  if (!s.open(camera_path, &err) || !s.start(2, backend, &err)) {
    r.status = TestStatus::Error;
    r.summary = "Session setup failed: " + err;
    return;
  }
  s.warmup(trigger);

  int cliff_delay = -1;
  for (int d = 0; d < N; d++) {
    const int delay = delays_ms[d];
    int hits = 0;
    std::vector<double> lat;
    for (int rep = 0; rep < REPS; rep++) {
      auto f1 = s.capture(trigger, 100, true, false);
      if (!f1.success)
        continue;
      if (delay > 0)
        V4lSession::sleep_ms(delay);
      s.requeue(f1.index);
      auto f2 = s.capture(trigger, 100, false, true);
      if (f2.success) {
        hits++;
        lat.push_back(f2.latency_ms);
      }
      V4lSession::sleep_ms(100);
    }
    if (hits < REPS && cliff_delay < 0)
      cliff_delay = delay;

    std::ostringstream d11;
    d11 << "delay=" << delay << "ms hits=" << hits << "/" << REPS;
    if (!lat.empty())
      d11 << " mean=" << static_cast<int>(compute_stats(lat).mean) << "ms";
    r.details.push_back(d11.str());
    r.metrics.push_back(metric("hits_delay_" + std::to_string(delay) + "ms", "count", static_cast<double>(hits),
                               "Hits at " + std::to_string(delay) + "ms delay."));
  }

  r.metrics.push_back(
      metric("cliff_delay_ms", "ms", static_cast<double>(cliff_delay), "First delay causing misses (-1 = none)."));
  if (cliff_delay < 0) {
    r.status = TestStatus::Pass;
    r.summary = "No buffer recycling sensitivity up to 100ms.";
  } else {
    const int min_safe = static_cast<int>(th.count("min_safe_cliff_delay_ms") ? th.at("min_safe_cliff_delay_ms") : 50);
    r.status = cliff_delay >= min_safe ? TestStatus::Pass : TestStatus::Warn;
    r.summary = "Buffer recycling cliff at " + std::to_string(cliff_delay) + "ms delay.";
  }
}

// Docs: docs/backend/tests/t12-stream-cycles.md
void run_t12(const std::string &camera_path, MemoryBackend backend, TriggerSource &trigger, TestResult &r,
             const LogFn &log, const TestThresholds &th) {
  constexpr int FULL = 20, RAPID = 50;
  int full_failures = 0;
  std::vector<double> first_lat;
  emit(log, camera_path, "t12", "STREAMON/STREAMOFF cycles: 20 full + 50 rapid...");

  for (int c = 0; c < FULL; c++) {
    V4lSession s;
    std::string err;
    if (!s.open(camera_path, &err) || !s.start(2, backend, &err)) {
      full_failures++;
      continue;
    }
    s.warmup(trigger, 3, 200);
    int ok = 0;
    for (int i = 0; i < 5; i++) {
      auto f = s.capture(trigger, 100);
      if (f.success) {
        ok++;
        if (i == 0)
          first_lat.push_back(f.latency_ms);
      }
      V4lSession::sleep_ms(100);
    }
    if (ok < 5)
      full_failures++;
  }

  int rapid_ok = 0;
  for (int c = 0; c < RAPID; c++) {
    V4lSession s;
    std::string err;
    if (!s.open(camera_path, &err) || !s.start(2, backend, &err))
      continue;
    if (s.capture(trigger, 200).success)
      rapid_ok++;
    V4lSession::sleep_ms(10);
  }

  r.metrics.push_back(
      metric("full_cycles_success", "count", static_cast<double>(FULL - full_failures), "Full cycles OK."));
  r.metrics.push_back(
      metric("full_cycle_failures", "count", static_cast<double>(full_failures), "Full cycle failures."));
  r.metrics.push_back(metric("rapid_cycles_ok", "count", static_cast<double>(rapid_ok), "Rapid cycles with frame."));
  r.metrics.push_back(metric("rapid_cycles_total", "count", static_cast<double>(RAPID), "Total rapid cycles."));
  if (!first_lat.empty())
    push_stats_metrics(r.metrics, "first_frame_latency", compute_stats(first_lat));

  const double rapid_pct = 100.0 * rapid_ok / RAPID;
  const int max_fail_pass = static_cast<int>(th.count("max_full_failures_pass") ? th.at("max_full_failures_pass") : 0);
  const int max_fail_warn = static_cast<int>(th.count("max_full_failures_warn") ? th.at("max_full_failures_warn") : 2);
  const double rapid_pass = th.count("rapid_pct_pass") ? th.at("rapid_pct_pass") : 90.0;
  const double rapid_warn = th.count("rapid_pct_warn") ? th.at("rapid_pct_warn") : 70.0;
  if (full_failures <= max_fail_pass && rapid_pct >= rapid_pass)
    r.status = TestStatus::Pass;
  else if (full_failures <= max_fail_warn && rapid_pct >= rapid_warn)
    r.status = TestStatus::Warn;
  else
    r.status = TestStatus::Fail;
  r.summary = "Full: " + std::to_string(FULL - full_failures) + "/" + std::to_string(FULL) +
              " OK. Rapid: " + std::to_string(rapid_ok) + "/" + std::to_string(RAPID) + " captured.";
}

// Docs: docs/backend/tests/t13-buffer-flags.md
void run_t13(const std::string &camera_path, MemoryBackend backend, TriggerSource &trigger, TestResult &r,
             const LogFn &log, const TestThresholds &th) {
  constexpr int NUM = 50;
  emit(log, camera_path, "t13", "Buffer flag analysis: capturing 50 frames...");
  V4lSession s;
  std::string err;
  if (!s.open(camera_path, &err) || !s.start(2, backend, &err)) {
    r.status = TestStatus::Error;
    r.summary = "Session setup failed: " + err;
    return;
  }
  s.warmup(trigger);

  int flag_error = 0, flag_keyframe = 0, flag_ts_mono = 0, flag_ts_copy = 0, flag_soe = 0, flag_eof = 0;
  uint32_t all_or = 0;
  int captured = 0;

  for (int i = 0; i < NUM; i++) {
    auto f = s.capture(trigger, 100);
    if (!f.success) {
      V4lSession::sleep_ms(100);
      continue;
    }
    captured++;
    all_or |= f.flags;
    if (f.flags & V4L2_BUF_FLAG_ERROR)
      flag_error++;
    if (f.flags & V4L2_BUF_FLAG_KEYFRAME)
      flag_keyframe++;
    if (f.flags & V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC)
      flag_ts_mono++;
    if (f.flags & V4L2_BUF_FLAG_TIMESTAMP_COPY)
      flag_ts_copy++;
    if (f.flags & V4L2_BUF_FLAG_TSTAMP_SRC_SOE)
      flag_soe++;
    else
      flag_eof++;
    V4lSession::sleep_ms(100);
  }

  r.metrics.push_back(metric("frames_captured", "count", static_cast<double>(captured), "Frames captured."));
  r.metrics.push_back(metric("flag_error", "count", static_cast<double>(flag_error), "V4L2_BUF_FLAG_ERROR."));
  r.metrics.push_back(metric("flag_keyframe", "count", static_cast<double>(flag_keyframe), "KEYFRAME."));
  r.metrics.push_back(metric("flag_ts_monotonic", "count", static_cast<double>(flag_ts_mono), "TIMESTAMP_MONOTONIC."));
  r.metrics.push_back(metric("flag_ts_copy", "count", static_cast<double>(flag_ts_copy), "TIMESTAMP_COPY."));
  r.metrics.push_back(metric("flag_soe", "count", static_cast<double>(flag_soe), "TSTAMP_SRC_SOE."));
  r.metrics.push_back(metric("flag_eof", "count", static_cast<double>(flag_eof), "TSTAMP_SRC_EOF."));

  char hex[16];
  snprintf(hex, sizeof(hex), "0x%08x", all_or);
  r.details.push_back("Combined flags OR: " + std::string(hex));
  const bool ts_ok = (flag_ts_mono == captured || flag_ts_copy == captured || (flag_ts_mono == 0 && flag_ts_copy == 0));
  r.details.push_back(std::string("Timestamp source consistent: ") + (ts_ok ? "yes" : "no"));

  if (flag_error > static_cast<int>(th.count("max_error_flags") ? th.at("max_error_flags") : 0)) {
    r.status = TestStatus::Warn;
    r.summary = std::to_string(flag_error) + " frames with V4L2_BUF_FLAG_ERROR.";
  } else {
    r.status = TestStatus::Pass;
    r.summary = "No error flags. Timestamp source " + std::string(ts_ok ? "consistent." : "inconsistent.");
  }
}

// Docs: docs/backend/tests/t14-timestamp-monotonicity.md
void run_t14(const std::string &camera_path, MemoryBackend backend, TriggerSource &trigger, TestResult &r,
             const LogFn &log, const TestThresholds &th) {
  constexpr int NUM = 100;
  emit(log, camera_path, "t14", "Timestamp monotonicity: capturing 100 frames...");
  V4lSession s;
  std::string err;
  if (!s.open(camera_path, &err) || !s.start(2, backend, &err)) {
    r.status = TestStatus::Error;
    r.summary = "Session setup failed: " + err;
    return;
  }
  s.warmup(trigger);

  std::vector<double> buf_ts, wall_ts;
  for (int i = 0; i < NUM; i++) {
    auto f = s.capture(trigger, 100);
    if (f.success) {
      buf_ts.push_back(f.timestamp.tv_sec * 1e6 + f.timestamp.tv_usec);
      wall_ts.push_back(f.t_recv.tv_sec * 1e6 + f.t_recv.tv_nsec / 1000.0);
    }
    V4lSession::sleep_ms(100);
  }

  if (buf_ts.size() < 2) {
    r.status = TestStatus::Error;
    r.summary = "Insufficient frames.";
    return;
  }

  std::vector<double> deltas_ms, offsets_ms;
  int non_mono = 0;
  for (size_t i = 1; i < buf_ts.size(); i++) {
    deltas_ms.push_back((buf_ts[i] - buf_ts[i - 1]) / 1000.0);
    if (buf_ts[i] <= buf_ts[i - 1])
      non_mono++;
  }
  for (size_t i = 0; i < buf_ts.size(); i++)
    offsets_ms.push_back((wall_ts[i] - buf_ts[i]) / 1000.0);

  push_stats_metrics(r.metrics, "delta", compute_stats(deltas_ms));
  push_stats_metrics(r.metrics, "wall_buf_offset", compute_stats(offsets_ms));
  r.metrics.push_back(metric("non_monotonic", "count", static_cast<double>(non_mono), "Non-monotonic count."));

  const int max_nm = static_cast<int>(th.count("max_non_monotonic") ? th.at("max_non_monotonic") : 0);
  if (non_mono <= max_nm) {
    r.status = TestStatus::Pass;
    r.summary = "Timestamps monotonic across " + std::to_string(buf_ts.size()) + " frames.";
  } else {
    r.status = TestStatus::Fail;
    r.summary = std::to_string(non_mono) + " non-monotonic timestamps.";
  }
}

// Docs: docs/backend/tests/t15-memory-throughput.md
void run_t15(const std::string &camera_path, MemoryBackend backend, TestResult &r, const LogFn &log) {
  emit(log, camera_path, "t15", "Memory throughput benchmark: 100 reps per buffer type...");
  V4lSession s;
  std::string err;
  if (!s.open(camera_path, &err) || !s.setup_buffers(2, backend, &err)) {
    r.status = TestStatus::Error;
    r.summary = "Buffer setup failed: " + err;
    return;
  }
  if (s.buffers().empty() || !s.buffers()[0].data()) {
    r.status = TestStatus::Error;
    r.summary = "No mapped buffers available.";
    return;
  }

  const size_t frame_sz = s.buffers()[0].length;
  std::vector<uint8_t> dst(frame_sz);
  constexpr int REPS = 100;

  r.details.push_back("Buffer size: " + std::to_string(frame_sz) + " bytes");
  r.metrics.push_back(metric("frame_size_bytes", "bytes", static_cast<double>(frame_sz), "Device buffer size."));

  const auto bench = [&](const std::string &label, const void *src, size_t sz) {
    if (!src || sz == 0)
      return;
    struct timespec t0, t1;
    clock_gettime(CLOCK_REALTIME, &t0);
    for (int i = 0; i < REPS; i++)
      memcpy(dst.data(), src, sz);
    clock_gettime(CLOCK_REALTIME, &t1);
    const double mbps = (static_cast<double>(sz) * REPS / 1048576.0) / (V4lSession::ts_diff_ms(t1, t0) / 1000.0);
    r.metrics.push_back(metric(label + "_mbps", "MB/s", mbps, label + " memcpy throughput."));
    r.details.push_back(label + ": " + std::to_string(static_cast<int>(mbps)) + " MB/s");
  };

  // Dmabuf negotiates its buffers as plain mmap first (see V4lSession),
  // then exports each one as a dma-buf fd — so its raw device buffer is
  // still an mmap pointer; label it "mmap" here to avoid colliding with
  // the "dmabuf_*" metrics from the dma_start path benchmarked below.
  const std::string prefix = (backend == MemoryBackend::Dmabuf) ? "mmap" : to_string(backend);
  const void *mp = s.buffers()[0].data();
  bench(prefix + "_full", mp, frame_sz);
  bench(prefix + "_4k", mp, std::min(frame_sz, static_cast<size_t>(4096)));
  bench(prefix + "_64k", mp, std::min(frame_sz, static_cast<size_t>(65536)));

  const void *dp = s.buffers()[0].dma_start;
  if (dp) {
    bench("dmabuf_full", dp, frame_sz);
    bench("dmabuf_4k", dp, std::min(frame_sz, static_cast<size_t>(4096)));
    bench("dmabuf_64k", dp, std::min(frame_sz, static_cast<size_t>(65536)));
#if V4L2DIAG_HAS_DMA_BUF_SYNC
    const int dfd = s.buffers()[0].dma_fd;
    if (dfd >= 0) {
      struct timespec t0, t1;
      clock_gettime(CLOCK_REALTIME, &t0);
      for (int i = 0; i < REPS; i++) {
        struct dma_buf_sync sync;
        sync.flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_READ;
        ioctl(dfd, DMA_BUF_IOCTL_SYNC, &sync);
        memcpy(dst.data(), dp, frame_sz);
        sync.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_READ;
        ioctl(dfd, DMA_BUF_IOCTL_SYNC, &sync);
      }
      clock_gettime(CLOCK_REALTIME, &t1);
      const double mbps =
          (static_cast<double>(frame_sz) * REPS / 1048576.0) / (V4lSession::ts_diff_ms(t1, t0) / 1000.0);
      r.metrics.push_back(metric("dmabuf_sync_mbps", "MB/s", mbps, "dmabuf full + DMA_BUF_IOCTL_SYNC."));
    }
#endif
  }
  r.status = TestStatus::Pass;
  r.summary = "Memory throughput benchmark complete on device-mapped buffers.";
}

// Docs: docs/backend/tests/t17-pollerr-handling.md
void run_t17(const std::string &camera_path, MemoryBackend backend, TriggerSource &trigger, TestResult &r,
             const LogFn &log, const TestThresholds &th) {
  emit(log, camera_path, "t17", "POLLERR/POLLHUP handling: testing STREAMOFF recovery...");
  V4lSession s;
  std::string err;
  if (!s.open(camera_path, &err) || !s.start(2, backend, &err)) {
    r.status = TestStatus::Error;
    r.summary = "Session setup failed: " + err;
    return;
  }
  s.warmup(trigger, 3, 200);

  int baseline_ok = 0;
  for (int i = 0; i < 3; i++) {
    if (s.capture(trigger, 100).success)
      baseline_ok++;
    V4lSession::sleep_ms(100);
  }
  s.streamoff();

  struct pollfd pfd = {s.fd(), POLLIN, 0};
  trigger.send();
  const int poll_ret = poll(&pfd, 1, 100);
  const bool pollerr = (pfd.revents & POLLERR) != 0;
  const bool pollhup = (pfd.revents & POLLHUP) != 0;

  struct v4l2_buffer buf;
  memset(&buf, 0, sizeof(buf));
  buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  buf.memory = V4L2_MEMORY_MMAP;
  const int dq_ret = ioctl(s.fd(), VIDIOC_DQBUF, &buf);
  const int dq_errno = errno;

  std::string so_err;
  const bool re_ok = s.streamon(&so_err);
  int recovery_ok = 0;
  if (re_ok) {
    s.warmup(trigger, 3, 200);
    for (int i = 0; i < 3; i++) {
      if (s.capture(trigger, 100).success)
        recovery_ok++;
      V4lSession::sleep_ms(100);
    }
  }

  r.metrics.push_back(metric("baseline_ok", "count", static_cast<double>(baseline_ok), "Baseline frames."));
  r.metrics.push_back(metric("pollerr_raised", "bool", pollerr ? 1.0 : 0.0, "POLLERR after STREAMOFF."));
  r.metrics.push_back(metric("pollhup_raised", "bool", pollhup ? 1.0 : 0.0, "POLLHUP after STREAMOFF."));
  r.metrics.push_back(
      metric("dqbuf_failed", "bool", dq_ret < 0 ? 1.0 : 0.0, "DQBUF correctly failed after STREAMOFF."));
  r.metrics.push_back(metric("restreamon_ok", "bool", re_ok ? 1.0 : 0.0, "Re-STREAMON succeeded."));
  r.metrics.push_back(metric("recovery_ok", "count", static_cast<double>(recovery_ok), "Recovery frames."));
  r.details.push_back("poll ret=" + std::to_string(poll_ret) + (pollerr ? " POLLERR" : "") +
                      (pollhup ? " POLLHUP" : ""));
  r.details.push_back("DQBUF after STREAMOFF: " + std::string(dq_ret < 0 ? "failed" : "succeeded") +
                      " errno=" + std::to_string(dq_errno) + " (" + strerror(dq_errno) + ")");

  const int min_rec = static_cast<int>(th.count("min_recovery_ok") ? th.at("min_recovery_ok") : 2);
  if (dq_ret < 0 && re_ok && recovery_ok >= min_rec) {
    r.status = TestStatus::Pass;
    r.summary = "STREAMOFF correctly prevents DQBUF; recovery OK (" + std::to_string(recovery_ok) + "/3).";
  } else if (dq_ret >= 0) {
    r.status = TestStatus::Fail;
    r.summary = "DQBUF succeeded after STREAMOFF — unexpected.";
  } else {
    r.status = TestStatus::Warn;
    r.summary = "DQBUF failed correctly but recovery partial (" + std::to_string(recovery_ok) + "/3).";
  }
}

// Docs: docs/backend/tests/t18-dmabuf-cache-sync.md
void run_t18(const std::string &camera_path, TriggerSource &trigger, TestResult &r, const LogFn &log,
             const TestThresholds &th) {
#if !V4L2DIAG_HAS_DMA_BUF_SYNC
  r.status = TestStatus::Skipped;
  r.summary = "linux/dma-buf.h not available; skipping cache coherency test.";
#else
  constexpr int NUM = 20, CMP = 64;
  emit(log, camera_path, "t18", "DMA_BUF_IOCTL_SYNC cache coherency: 20 frames...");
  V4lSession s;
  std::string err;
  if (!s.open(camera_path, &err) || !s.start(2, MemoryBackend::Dmabuf, &err)) {
    r.status = TestStatus::Error;
    r.summary = "DMABUF session failed: " + err;
    return;
  }
  s.warmup(trigger);

  int tested = 0, match_nosync = 0, match_sync = 0;
  for (int i = 0; i < NUM; i++) {
    auto f = s.capture(trigger, 100, true, false);
    if (!f.success || f.index >= s.buffer_count() || f.bytesused < CMP) {
      if (f.success)
        s.requeue(f.index);
      continue;
    }
    const void *mp = s.buffers()[f.index].mmap_start, *dp = s.buffers()[f.index].dma_start;
    const int dfd = s.buffers()[f.index].dma_fd;
    if (!mp || !dp || dfd < 0) {
      s.requeue(f.index);
      continue;
    }
    tested++;
    uint8_t mmap_d[CMP], nosync_d[CMP], sync_d[CMP];
    memcpy(mmap_d, mp, CMP);
    memcpy(nosync_d, dp, CMP);
    if (memcmp(mmap_d, nosync_d, CMP) == 0)
      match_nosync++;
    struct dma_buf_sync sync;
    sync.flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_READ;
    ioctl(dfd, DMA_BUF_IOCTL_SYNC, &sync);
    memcpy(sync_d, dp, CMP);
    sync.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_READ;
    ioctl(dfd, DMA_BUF_IOCTL_SYNC, &sync);
    if (memcmp(mmap_d, sync_d, CMP) == 0)
      match_sync++;
    s.requeue(f.index);
    V4lSession::sleep_ms(100);
  }

  r.metrics.push_back(metric("frames_tested", "count", static_cast<double>(tested), "Frames compared."));
  r.metrics.push_back(
      metric("match_without_sync", "count", static_cast<double>(match_nosync), "Matches without sync."));
  r.metrics.push_back(metric("match_with_sync", "count", static_cast<double>(match_sync), "Matches with sync."));
  r.metrics.push_back(
      metric("sync_required", "bool", (match_nosync < tested && match_sync == tested) ? 1.0 : 0.0, "Sync required."));

  if (tested == 0) {
    r.status = TestStatus::Error;
    r.summary = "No frames compared.";
  } else {
    const double min_ratio = th.count("min_match_ratio") ? th.at("min_match_ratio") : 1.0;
    const double ratio = static_cast<double>(match_sync) / tested;
    if (ratio >= min_ratio) {
      r.status = TestStatus::Pass;
      r.summary = match_nosync < tested ? "Sync IS required on this platform." : "Coherent DMA — sync not required.";
    } else {
      r.status = TestStatus::Fail;
      r.summary =
          "Cache coherency failure: " + std::to_string(match_sync) + "/" + std::to_string(tested) + " match with sync.";
    }
  }
#endif
}

// Docs: docs/backend/tests/t19-gpio-pulse-width.md
void run_t19(const std::string &camera_path, MemoryBackend backend, TriggerSource &trigger, TestResult &r,
             const LogFn &log) {
  static const int pws[] = {1, 2, 3, 5, 7, 10, 13, 15, 20, 25, 30};
  constexpr int N = static_cast<int>(sizeof(pws) / sizeof(pws[0]));
  constexpr int SAMPLES = 8;
  emit(log, camera_path, "t19", "GPIO pulse width sweep: 11 widths x 8 samples...");

  V4lSession s;
  std::string err;
  if (!s.open(camera_path, &err) || !s.start(2, backend, &err)) {
    r.status = TestStatus::Error;
    r.summary = "Session setup failed: " + err;
    return;
  }
  s.warmup(trigger, 5, 200);

  double sum_rh = 0.0, sum_rl = 0.0;
  int full_rows = 0;
  for (int wi = 0; wi < N; wi++) {
    const int pw = pws[wi];
    const uint64_t pw_ns = static_cast<uint64_t>(pw) * 1'000'000UL;
    double sh = 0.0, sl = 0.0, minh = 1e9, maxh = 0.0, minl = 1e9, maxl = 0.0;
    int hits = 0;
    for (int smpl = 0; smpl < SAMPLES; smpl++) {
      s.drain();
      V4lSession::sleep_ms(10);
      struct timespec t_high = trigger.send(pw_ns);
      // Approximate LOW edge time
      struct timespec t_low = t_high;
      t_low.tv_nsec += static_cast<long>(pw_ns % 1'000'000'000UL);
      t_low.tv_sec += static_cast<long>(pw_ns / 1'000'000'000UL);
      if (t_low.tv_nsec >= 1'000'000'000L) {
        t_low.tv_sec++;
        t_low.tv_nsec -= 1'000'000'000L;
      }

      struct pollfd pfd = {s.fd(), POLLIN, 0};
      if (poll(&pfd, 1, 500) > 0 && (pfd.revents & POLLIN)) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        if (ioctl(s.fd(), VIDIOC_DQBUF, &buf) == 0) {
          struct timespec t_recv;
          clock_gettime(CLOCK_REALTIME, &t_recv);
          ioctl(s.fd(), VIDIOC_QBUF, &buf);
          const double lh = V4lSession::ts_diff_ms(t_recv, t_high);
          const double ll = V4lSession::ts_diff_ms(t_recv, t_low);
          sh += lh;
          sl += ll;
          minh = std::min(minh, lh);
          maxh = std::max(maxh, lh);
          minl = std::min(minl, ll);
          maxl = std::max(maxl, ll);
          hits++;
        }
      }
    }
    const std::string pstr = std::to_string(pw);
    r.metrics.push_back(metric("hits_" + pstr + "ms", "count", static_cast<double>(hits), "Hits at " + pstr + "ms."));
    if (hits > 0) {
      r.metrics.push_back(
          metric("lat_high_avg_" + pstr + "ms", "ms", sh / hits, "Mean lat from HIGH at " + pstr + "ms."));
      r.metrics.push_back(
          metric("lat_low_avg_" + pstr + "ms", "ms", sl / hits, "Mean lat from LOW at " + pstr + "ms."));
      r.details.push_back(pstr + "ms: hits=" + std::to_string(hits) + "/" + std::to_string(SAMPLES) +
                          " lat_HIGH=" + std::to_string(static_cast<int>(sh / hits)) + "ms" +
                          " lat_LOW=" + std::to_string(static_cast<int>(sl / hits)) + "ms");
      if (hits == SAMPLES) {
        sum_rh += (maxh - minh);
        sum_rl += (maxl - minl);
        full_rows++;
      }
    } else {
      r.details.push_back(pstr + "ms: 0/" + std::to_string(SAMPLES) + " (all missed)");
    }
  }

  std::string edge = "inconclusive";
  if (full_rows > 0) {
    const double rh = sum_rh / full_rows, rl = sum_rl / full_rows;
    r.metrics.push_back(metric("range_h", "ms", rh, "Avg within-level range of lat_HIGH."));
    r.metrics.push_back(metric("range_l", "ms", rl, "Avg within-level range of lat_LOW."));
    if (rh < rl - 1.0)
      edge = "rising";
    else if (rl < rh - 1.0)
      edge = "falling";
    r.details.push_back("Edge detection: " + edge);
  }
  r.status = TestStatus::Pass;
  r.summary = "GPIO pulse width sweep complete. Trigger edge: " + edge + ".";
}

// Docs: docs/backend/tests/t20-camera-controls.md
void run_t20(const std::string &camera_path, TriggerSource &trigger, MemoryBackend backend, TestResult &r,
             const LogFn &log) {
  emit(log, camera_path, "t20", "Camera control inventory + ISX021 sweep...");
  int fd = ::open(camera_path.c_str(), O_RDWR | O_NONBLOCK);
  if (fd < 0) {
    r.status = TestStatus::Error;
    r.summary = "Cannot open device: " + std::string(strerror(errno));
    return;
  }

  constexpr uint32_t ISX_LL = 0x9a206d, ISX_BP = 0x9a2064, ISX_WI = 0x9a2068;
  int ctrl_count = 0, writable_count = 0;
  bool has_isx = false;

  struct v4l2_queryctrl qc;
  memset(&qc, 0, sizeof(qc));
  qc.id = V4L2_CTRL_FLAG_NEXT_CTRL;
  while (ioctl(fd, VIDIOC_QUERYCTRL, &qc) == 0) {
    if (!(qc.flags & V4L2_CTRL_FLAG_DISABLED)) {
      ctrl_count++;
      if (!(qc.flags & V4L2_CTRL_FLAG_READ_ONLY))
        writable_count++;
      struct v4l2_control cur;
      cur.id = qc.id;
      ioctl(fd, VIDIOC_G_CTRL, &cur);
      char hex[9];
      snprintf(hex, sizeof(hex), "%08x", qc.id);
      r.details.push_back(std::string(reinterpret_cast<char *>(qc.name)) + " [0x" + hex + "]" +
                          " min=" + std::to_string(qc.minimum) + " max=" + std::to_string(qc.maximum) +
                          " step=" + std::to_string(qc.step) + " def=" + std::to_string(qc.default_value) +
                          " cur=" + std::to_string(cur.value) + ((qc.flags & V4L2_CTRL_FLAG_READ_ONLY) ? " [RO]" : ""));
      if (qc.id == ISX_LL || qc.id == ISX_BP || qc.id == ISX_WI)
        has_isx = true;
    }
    qc.id |= V4L2_CTRL_FLAG_NEXT_CTRL;
  }

  r.metrics.push_back(metric("control_count", "count", static_cast<double>(ctrl_count), "Total controls."));
  r.metrics.push_back(metric("writable_count", "count", static_cast<double>(writable_count), "Writable controls."));
  r.metrics.push_back(metric("isx021_found", "bool", has_isx ? 1.0 : 0.0, "ISX021-specific controls found."));

  if (!has_isx) {
    ::close(fd);
    r.status = TestStatus::Pass;
    r.summary = std::to_string(ctrl_count) + " controls found. ISX021-specific controls unavailable.";
    return;
  }

  // ISX021 sweep
  auto get_c = [&](uint32_t cid) {
    struct v4l2_control c;
    c.id = cid;
    ioctl(fd, VIDIOC_G_CTRL, &c);
    return c.value;
  };
  auto set_c = [&](uint32_t cid, int val) {
    struct v4l2_control c;
    c.id = cid;
    c.value = val;
    return ioctl(fd, VIDIOC_S_CTRL, &c) == 0;
  };
  const int o_ll = get_c(ISX_LL), o_bp = get_c(ISX_BP), o_wi = get_c(ISX_WI);

  for (int ll = 0; ll <= 1; ll++)
    for (int bp = 0; bp <= 1; bp++)
      for (int wi = 0; wi <= 1; wi++) {
        set_c(ISX_LL, ll);
        set_c(ISX_BP, bp);
        set_c(ISX_WI, wi);
        V4lSession ss;
        std::string serr;
        ss.open(camera_path, &serr);
        if (!ss.start(2, backend, &serr))
          continue;
        ss.warmup(trigger, 8, 200);
        std::vector<double> lats;
        for (int i = 0; i < 20; i++) {
          auto f = ss.capture(trigger, 200);
          if (f.success)
            lats.push_back(f.latency_ms);
        }
        const std::string k = "ll" + std::to_string(ll) + "_bp" + std::to_string(bp) + "_wi" + std::to_string(wi);
        if (!lats.empty()) {
          const Stats st = compute_stats(lats);
          r.metrics.push_back(metric(k + "_mean_ms", "ms", st.mean, k + " latency mean."));
          r.details.push_back(k + ": n=" + std::to_string(lats.size()) + " mean=" + std::to_string(st.mean) + "ms");
        }
      }
  set_c(ISX_LL, o_ll);
  set_c(ISX_BP, o_bp);
  set_c(ISX_WI, o_wi);
  ::close(fd);
  r.status = TestStatus::Pass;
  r.summary = "ISX021 control sweep complete. " + std::to_string(ctrl_count) + " total controls.";
}

// Docs: docs/backend/tests/t21-stuck-frame.md
void run_t21(const std::string &camera_path, MemoryBackend backend, TriggerSource &trigger, TestResult &r,
             const LogFn &log, const TestThresholds &th) {
  constexpr int NUM = 50;
  constexpr size_t CMP = 4096;
  emit(log, camera_path, "t21", "Stuck frame detection: comparing 50 consecutive frames...");

  V4lSession s;
  std::string err;
  if (!s.open(camera_path, &err) || !s.start(2, backend, &err)) {
    r.status = TestStatus::Error;
    r.summary = "Session setup failed: " + err;
    return;
  }
  s.warmup(trigger);

  std::vector<uint8_t> prev(CMP, 0);
  int identical = 0, max_run = 0, cur_run = 0, tested = 0;

  for (int i = 0; i < NUM; i++) {
    auto f = s.capture(trigger, 100, true, false);
    if (!f.success || f.index >= s.buffer_count() || f.bytesused < CMP) {
      if (f.success)
        s.requeue(f.index);
      V4lSession::sleep_ms(100);
      continue;
    }
    const void *src = s.buffers()[f.index].data();
    if (!src) {
      s.requeue(f.index);
      continue;
    }
    tested++;
    std::vector<uint8_t> cur(CMP);
    memcpy(cur.data(), src, CMP);
    s.requeue(f.index);
    if (tested > 1 && memcmp(prev.data(), cur.data(), CMP) == 0) {
      identical++;
      cur_run++;
      max_run = std::max(max_run, cur_run);
    } else {
      cur_run = 0;
    }
    prev = cur;
    V4lSession::sleep_ms(100);
  }

  r.metrics.push_back(metric("frames_tested", "count", static_cast<double>(tested), "Frames compared."));
  r.metrics.push_back(
      metric("identical_pairs", "count", static_cast<double>(identical), "Identical consecutive pairs."));
  r.metrics.push_back(metric("max_identical_run", "count", static_cast<double>(max_run), "Max identical run length."));

  if (tested < 2) {
    r.status = TestStatus::Error;
    r.summary = "Insufficient frames.";
  } else if (identical == 0) {
    r.status = TestStatus::Pass;
    r.summary = "All " + std::to_string(tested) + " frames unique. No stuck frame.";
  } else if (max_run >= static_cast<int>(th.count("max_identical_run") ? th.at("max_identical_run") : 5)) {
    r.status = TestStatus::Fail;
    r.summary = "Camera appears stuck: " + std::to_string(max_run) + " consecutive identical frames.";
  } else {
    r.status = TestStatus::Warn;
    r.summary = std::to_string(identical) + " identical pairs; max run=" + std::to_string(max_run) + ".";
  }
}

// Docs: docs/backend/tests/t22-latency-under-load.md
void run_t22(const std::string &camera_path, MemoryBackend backend, TriggerSource &trigger, TestResult &r,
             const LogFn &log, const TestThresholds &th) {
  constexpr int SAMPLES = 30, LOAD_THREADS = 4;
  emit(log, camera_path, "t22", "Latency under CPU load: baseline 30 samples, then 4-thread stress...");

  // Baseline
  std::vector<double> baseline;
  {
    V4lSession s;
    std::string err;
    if (!s.open(camera_path, &err) || !s.start(2, backend, &err)) {
      r.status = TestStatus::Error;
      r.summary = "Baseline session failed: " + err;
      return;
    }
    s.warmup(trigger);
    for (int i = 0; i < SAMPLES; i++) {
      auto f = s.capture(trigger, 100);
      if (f.success)
        baseline.push_back(f.latency_ms);
      V4lSession::sleep_ms(200);
    }
  }

  if (baseline.empty()) {
    r.status = TestStatus::Fail;
    r.summary = "No baseline frames.";
    return;
  }

  // Start CPU load threads
  std::atomic<bool> stop{false};
  std::vector<std::thread> threads;
  threads.reserve(LOAD_THREADS);
  for (int i = 0; i < LOAD_THREADS; i++) {
    threads.emplace_back([&stop]() {
      volatile uint64_t x = 1;
      while (!stop.load(std::memory_order_relaxed))
        x ^= x * 6364136223846793005ULL + 1;
    });
  }

  // Under-load capture
  std::vector<double> under_load;
  {
    V4lSession s;
    std::string err;
    if (s.open(camera_path, &err) && s.start(2, backend, &err)) {
      s.warmup(trigger);
      for (int i = 0; i < SAMPLES; i++) {
        auto f = s.capture(trigger, 200);
        if (f.success)
          under_load.push_back(f.latency_ms);
        V4lSession::sleep_ms(200);
      }
    }
  }

  stop = true;
  for (auto &t : threads)
    t.join();

  const Stats sb = compute_stats(baseline);
  push_stats_metrics(r.metrics, "baseline_latency", sb);
  r.metrics.push_back(metric("baseline_captures", "count", static_cast<double>(baseline.size()), "Baseline frames."));

  if (!under_load.empty()) {
    const Stats sl = compute_stats(under_load);
    push_stats_metrics(r.metrics, "load_latency", sl);
    r.metrics.push_back(metric("load_captures", "count", static_cast<double>(under_load.size()), "Under-load frames."));
    const double dp95 = sl.p95 - sb.p95;
    const double dm = sl.mean - sb.mean;
    r.metrics.push_back(metric("delta_mean_ms", "ms", dm, "Mean latency increase under load."));
    r.metrics.push_back(metric("delta_p95_ms", "ms", dp95, "p95 latency increase under load."));

    if (dp95 < (th.count("pass_delta_p95_ms") ? th.at("pass_delta_p95_ms") : 5.0)) {
      r.status = TestStatus::Pass;
      r.summary = "CPU load impact minimal: p95 delta=" + std::to_string(dp95) + "ms.";
    } else if (dp95 < (th.count("warn_delta_p95_ms") ? th.at("warn_delta_p95_ms") : 20.0)) {
      r.status = TestStatus::Warn;
      r.summary = "Moderate CPU load impact: p95 delta=" + std::to_string(dp95) + "ms.";
    } else {
      r.status = TestStatus::Fail;
      r.summary = "High CPU load impact: p95 delta=" + std::to_string(dp95) + "ms.";
    }
  } else {
    r.status = TestStatus::Warn;
    r.summary = "No under-load frames captured.";
  }
}

// Docs: docs/backend/tests/t24-control-inventory.md
void run_t24(const std::string &camera_path, TestResult &r, const LogFn &log) {
  emit(log, camera_path, "t24", "Enumerating V4L2 controls...");
  int fd = ::open(camera_path.c_str(), O_RDWR | O_NONBLOCK);
  if (fd < 0) {
    r.status = TestStatus::Error;
    r.summary = "Cannot open device: " + std::string(strerror(errno));
    return;
  }

  int count = 0, writable = 0;
  struct v4l2_queryctrl qc;
  memset(&qc, 0, sizeof(qc));
  qc.id = V4L2_CTRL_FLAG_NEXT_CTRL;
  while (ioctl(fd, VIDIOC_QUERYCTRL, &qc) == 0) {
    if (!(qc.flags & V4L2_CTRL_FLAG_DISABLED)) {
      count++;
      const bool ro = (qc.flags & V4L2_CTRL_FLAG_READ_ONLY) != 0;
      if (!ro)
        writable++;
      struct v4l2_control cur;
      cur.id = qc.id;
      ioctl(fd, VIDIOC_G_CTRL, &cur);
      char hex[9];
      snprintf(hex, sizeof(hex), "%08x", qc.id);
      r.details.push_back(std::string(reinterpret_cast<char *>(qc.name)) + " [0x" + hex + "]" +
                          " min=" + std::to_string(qc.minimum) + " max=" + std::to_string(qc.maximum) +
                          " step=" + std::to_string(qc.step) + " default=" + std::to_string(qc.default_value) +
                          " current=" + std::to_string(cur.value) + (ro ? " [RO]" : ""));
    }
    qc.id |= V4L2_CTRL_FLAG_NEXT_CTRL;
  }
  ::close(fd);

  r.metrics.push_back(metric("control_count", "count", static_cast<double>(count), "Total controls."));
  r.metrics.push_back(metric("writable_count", "count", static_cast<double>(writable), "Writable controls."));
  r.status = TestStatus::Pass;
  r.summary = "Enumerated " + std::to_string(count) + " controls (" + std::to_string(writable) + " writable).";
}

}  // anonymous namespace

/* =========================================================================
 * DiagnosticRunner
 * ========================================================================= */

DiagnosticRunner::DiagnosticRunner(ProfileRegistry *profiles) : profiles_(profiles) {}

const TestThresholds &DiagnosticRunner::thresholds_for(const std::string &test_id) const {
  static const TestThresholds empty;
  auto it = active_thresholds_.values.find(test_id);
  return it != active_thresholds_.values.end() ? it->second : empty;
}

RunResult DiagnosticRunner::run(const RunConfig &config) {
  // Resolve threshold configuration once before any per-camera work.
  {
    const std::string threshold_dir =
        config.config_directory.empty() ? std::string() : config.config_directory + "/thresholds";
    ThresholdRegistry registry(threshold_dir);
    active_thresholds_ = registry.resolve(config.threshold_config_id.empty() ? "default" : config.threshold_config_id);
  }

  RunResult result;
  result.started_at_utc = utc_timestamp();
  result.host_name = host_name();
  result.output_directory = config.output_directory;
  result.run_mode = config.run_mode;

  const auto tests = select_tests(config.test_selectors, config.include_long_tests, config.include_experimental_tests);

  struct CameraJob {
    RunConfig::CameraConfig camera;
    DeviceProfile profile;
  };
  std::map<std::string, std::vector<CameraJob>> groups;
  int unassigned_index = 0;
  for (const auto &camera : config.cameras) {
    CameraJob job;
    job.camera = camera;
    if (!camera.profile_id.empty()) {
      profiles_->get_profile(camera.profile_id, &job.profile);
    }

    std::string resource_key = "camera:" + camera.path;
    if (config.trigger_mode != TriggerMode::FreeRun) {
      const auto channel =
          std::find_if(job.profile.trigger_channels.begin(), job.profile.trigger_channels.end(),
                       [&](const TriggerChannel &item) { return item.id == camera.trigger_channel_id; });
      if (channel != job.profile.trigger_channels.end() && config.trigger_mode == TriggerMode::Hardware &&
          channel->type == TriggerChannel::Type::Hardware) {
        resource_key =
            "gpio:" + std::to_string(channel->gpio.chip_id) + ":" + std::to_string(channel->gpio.line_number);
      } else if (channel != job.profile.trigger_channels.end() && config.trigger_mode == TriggerMode::Software &&
                 channel->type == TriggerChannel::Type::Software) {
        resource_key = "software:" + camera.profile_id + ":" + channel->id;
      } else {
        resource_key = "unassigned:" + std::to_string(unassigned_index++);
      }
    }
    groups[resource_key].push_back(std::move(job));
  }

  const auto run_group = [&](const std::vector<CameraJob> &group) {
    std::vector<CameraRunResult> camera_results;
    for (const auto &job : group) {
      camera_results.push_back(run_camera(job.camera, config, job.profile, tests));
    }
    return camera_results;
  };

  if (config.run_mode == RunMode::Parallel && groups.size() > 1) {
    std::vector<std::future<std::vector<CameraRunResult>>> futures;
    for (const auto &entry : groups) {
      futures.push_back(std::async(std::launch::async, [&, group = entry.second]() { return run_group(group); }));
    }
    for (auto &future : futures) {
      auto group_results = future.get();
      result.cameras.insert(result.cameras.end(), std::make_move_iterator(group_results.begin()),
                            std::make_move_iterator(group_results.end()));
    }
  } else {
    for (const auto &entry : groups) {
      auto group_results = run_group(entry.second);
      result.cameras.insert(result.cameras.end(), std::make_move_iterator(group_results.begin()),
                            std::make_move_iterator(group_results.end()));
    }
  }

  result.finished_at_utc = utc_timestamp();
  return result;
}

CameraRunResult DiagnosticRunner::run_camera(const RunConfig::CameraConfig &camera, const RunConfig &config,
                                             const DeviceProfile &profile, const std::vector<TestDefinition> &tests) {
  CameraRunResult camera_result;
  camera_result.camera_path = camera.path;
  camera_result.profile_id = camera.profile_id;
  camera_result.trigger_channel_id = camera.trigger_channel_id;
  camera_result.trigger_mode = config.trigger_mode;
  camera_result.memory_backends = config.memory_backends;

  std::unique_ptr<TriggerSource> trigger;
  std::string trigger_error;
  if (config.trigger_mode == TriggerMode::FreeRun) {
    trigger = std::make_unique<FreeRunTrigger>();
    camera_result.trigger_description = "free-run (no external trigger)";
  } else {
    const auto channel = std::find_if(profile.trigger_channels.begin(), profile.trigger_channels.end(),
                                      [&](const TriggerChannel &item) { return item.id == camera.trigger_channel_id; });
    if (channel == profile.trigger_channels.end()) {
      trigger_error =
          "trigger channel '" + camera.trigger_channel_id + "' was not found in profile '" + camera.profile_id + "'";
    } else {
      if (channel->type == TriggerChannel::Type::Hardware) {
        camera_result.trigger_description =
            "gpiochip" + std::to_string(channel->gpio.chip_id) + " line " + std::to_string(channel->gpio.line_number);
        if (!channel->gpio.description.empty())
          camera_result.trigger_description += " (" + channel->gpio.description + ")";
      } else {
        camera_result.trigger_description = "software: " + channel->name;
      }
      if (config.trigger_mode == TriggerMode::Hardware && channel->type == TriggerChannel::Type::Hardware) {
        auto source = std::make_unique<GpioTrigger>();
        if (source->open(channel->gpio, &trigger_error)) {
          trigger = std::move(source);
        }
      } else if (config.trigger_mode == TriggerMode::Software && channel->type == TriggerChannel::Type::Software) {
        auto source = std::make_unique<V4l2ControlTrigger>();
        if (source->open(camera.path, *channel, &trigger_error)) {
          trigger = std::move(source);
        }
      } else {
        trigger_error = "trigger channel type does not match run trigger mode";
      }
    }
  }

  for (MemoryBackend backend : config.memory_backends) {
    for (const auto &test : tests) {
      // Check for cancellation between tests.
      if (config.stop_token && config.stop_token->load(std::memory_order_relaxed)) {
        break;
      }
      auto test_result = run_test(camera.path, backend, test, config, profile, trigger.get(), trigger_error);
      if (config.progress_callback) {
        config.progress_callback(camera.path, test_result);
      }
      camera_result.tests.push_back(std::move(test_result));
    }
    if (config.stop_token && config.stop_token->load(std::memory_order_relaxed)) {
      break;
    }
  }
  return camera_result;
}

TestResult DiagnosticRunner::run_test(const std::string &camera_path, MemoryBackend backend,
                                      const TestDefinition &definition, const RunConfig &config,
                                      const DeviceProfile &profile, TriggerSource *trigger,
                                      const std::string &trigger_error) {
  (void)profile;
  TestResult result;
  result.id = definition.id;
  result.name = definition.name;
  result.category = definition.category;
  result.memory_backend = to_string(backend);
  const auto start = std::chrono::steady_clock::now();

  if (definition.requires_dmabuf && backend != MemoryBackend::Dmabuf) {
    result.status = TestStatus::Skipped;
    result.summary = "Test requires DMABUF and was skipped for backend " + std::string(to_string(backend)) + ".";
    result.duration_ms = elapsed_ms(start, std::chrono::steady_clock::now());
    return result;
  }

  if (!supports_trigger_mode(definition, config.trigger_mode)) {
    result.status = TestStatus::Skipped;
    result.summary = "Test is not compatible with trigger mode '" + std::string(to_string(config.trigger_mode)) + "'.";
    result.duration_ms = elapsed_ms(start, std::chrono::steady_clock::now());
    return result;
  }

  if (definition.uses_trigger && !trigger) {
    result.status = TestStatus::Skipped;
    result.summary = "Trigger source is unavailable for mode '" + std::string(to_string(config.trigger_mode)) + "'.";
    if (!trigger_error.empty())
      result.summary += " Reason: " + trigger_error;
    result.duration_ms = elapsed_ms(start, std::chrono::steady_clock::now());
    return result;
  }

  const LogFn &log = config.log_callback;
  emit_section(log, camera_path, definition.id,
               "\xe2\x96\xb6 " + definition.id + " \xe2\x80\x94 " + definition.name + " [" + to_string(backend) + "]");

  if (definition.id == "t01-no-streamon")
    run_t01(camera_path, backend, result, log);
  else if (definition.id == "t02-buffer-overwrite")
    run_t02(camera_path, backend, *trigger, result, log, thresholds_for(definition.id));
  else if (definition.id == "t03-gpio-latency")
    run_t03(camera_path, backend, *trigger, result, log);
  else if (definition.id == "t04-nonblock-vs-block")
    run_t04(camera_path, backend, *trigger, result, log);
  else if (definition.id == "t05-poll-timeout-effect")
    run_t05(camera_path, backend, *trigger, result, log);
  else if (definition.id == "t06-format-comparison")
    run_t06(camera_path, backend, *trigger, result, log);
  else if (definition.id == "t07-poll-timeout-sweep")
    run_t07(camera_path, backend, *trigger, result, log, thresholds_for(definition.id));
  else if (definition.id == "t08-sequence-continuity")
    run_t08(camera_path, backend, *trigger, result, log, thresholds_for(definition.id));
  else if (definition.id == "t09-sustained-capture")
    run_t09(camera_path, backend, *trigger, result, log, thresholds_for(definition.id));
  else if (definition.id == "t10-multi-buffer")
    run_t10(camera_path, backend, trigger, result, log);
  else if (definition.id == "t11-buffer-recycling")
    run_t11(camera_path, backend, *trigger, result, log, thresholds_for(definition.id));
  else if (definition.id == "t12-stream-cycles")
    run_t12(camera_path, backend, *trigger, result, log, thresholds_for(definition.id));
  else if (definition.id == "t13-buffer-flags")
    run_t13(camera_path, backend, *trigger, result, log, thresholds_for(definition.id));
  else if (definition.id == "t14-timestamp-monotonicity")
    run_t14(camera_path, backend, *trigger, result, log, thresholds_for(definition.id));
  else if (definition.id == "t15-memory-throughput")
    run_t15(camera_path, backend, result, log);
  // Docs: docs/backend/tests/t16-v4l2-compliance.md
  else if (definition.id == "t16-v4l2-compliance") {
    emit(log, camera_path, "t16", "V4L2 compliance check...");
    DeviceInfo info;
    if (!query_device(camera_path, &info)) {
      result.status = TestStatus::Error;
      result.summary = "Failed to query V4L2 device: " + info.error;
    } else {
      result.status = (info.supports_capture && info.supports_streaming) ? TestStatus::Pass : TestStatus::Fail;
      result.summary = info.supports_capture && info.supports_streaming
                           ? "Device exposes V4L2 capture and streaming capabilities."
                           : "Device does not expose the required V4L2 capture and streaming capabilities.";
      result.details.push_back("Driver: " + info.driver);
      result.details.push_back("Card: " + info.card);
      result.details.push_back("Bus: " + info.bus_info);
      result.details.push_back("Selected backend: " + std::string(to_string(backend)));
      result.metrics.push_back(metric("supports_capture", "bool", info.supports_capture ? 1.0 : 0.0,
                                      "Whether the device reports V4L2 capture support."));
      result.metrics.push_back(metric("supports_streaming", "bool", info.supports_streaming ? 1.0 : 0.0,
                                      "Whether the device reports V4L2 streaming support."));
      result.metrics.push_back(metric("format_count", "count", static_cast<double>(info.formats.size()),
                                      "Number of enumerated V4L2 pixel formats."));
      for (const auto &fmt : info.formats)
        result.details.push_back("Format: " + fmt.fourcc + " (" + fmt.description + ", " + fmt.buffer_type + ")");
    }
  } else if (definition.id == "t17-pollerr-handling")
    run_t17(camera_path, backend, *trigger, result, log, thresholds_for(definition.id));
  else if (definition.id == "t18-dmabuf-cache-sync")
    run_t18(camera_path, *trigger, result, log, thresholds_for(definition.id));
  else if (definition.id == "t19-gpio-pulse-width")
    run_t19(camera_path, backend, *trigger, result, log);
  else if (definition.id == "t20-camera-controls")
    run_t20(camera_path, *trigger, backend, result, log);
  else if (definition.id == "t21-stuck-frame")
    run_t21(camera_path, backend, *trigger, result, log, thresholds_for(definition.id));
  else if (definition.id == "t22-latency-under-load")
    run_t22(camera_path, backend, *trigger, result, log, thresholds_for(definition.id));
  else if (definition.id == "t24-control-inventory")
    run_t24(camera_path, result, log);
  // Docs: docs/backend/tests/v4l2-memory-probe.md
  else if (definition.id == "v4l2-memory-probe") {
    emit(log, camera_path, "v4l2-memory-probe", "Probing memory backend support...");
    const auto probes = probe_memory_backends(camera_path);
    bool selected_supported = false;
    for (const auto &probe : probes) {
      result.metrics.push_back(metric(std::string("backend_") + to_string(probe.backend), "bool",
                                      probe.supported ? 1.0 : 0.0, probe.detail));
      result.details.push_back(std::string(to_string(probe.backend)) + ": " +
                               (probe.supported ? "supported" : "unsupported") + " (" + probe.detail + ")");
      if (probe.backend == backend && probe.supported)
        selected_supported = true;
    }
    result.status = selected_supported ? TestStatus::Pass : TestStatus::Warn;
    result.summary = selected_supported ? "Selected memory backend is supported by VIDIOC_REQBUFS."
                                        : "Selected memory backend was not accepted by VIDIOC_REQBUFS.";
  } else {
    result.status = TestStatus::Skipped;
    result.summary = "No core implementation registered for this test.";
  }

  result.duration_ms = elapsed_ms(start, std::chrono::steady_clock::now());
  emit(log, camera_path, definition.id,
       "\xe2\x9c\x93 Completed in " + std::to_string(static_cast<int>(result.duration_ms)) + "ms", "info", "summary");
  return result;
}

}  // namespace v4l2diag
