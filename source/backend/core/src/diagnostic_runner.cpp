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

// Resolves a verdict threshold: the configured value from `th` if present,
// otherwise the built-in default for (test_id, key). The default table
// (default_threshold_config) is the single source of truth for the numbers —
// verdict sites never hard-code them. `th` is fully populated for the tests
// that have thresholds, so the fallback path is only a safety net.
double thv(const TestThresholds &th, const std::string &test_id, const std::string &key) {
  const auto it = th.find(key);
  if (it != th.end()) {
    return it->second;
  }
  return default_threshold_config().get(test_id, key);
}

// Resolves a run parameter: the configured value from `tp` if present,
// otherwise the built-in default from default_test_params().
double tpv(const TestThresholds &tp, const std::string &test_id, const std::string &key) {
  const auto it = tp.find(key);
  if (it != tp.end()) {
    return it->second;
  }
  const auto defaults = default_test_params();
  auto dt = defaults.find(test_id);
  if (dt != defaults.end()) {
    auto dk = dt->second.find(key);
    if (dk != dt->second.end()) {
      return dk->second;
    }
  }
  return 0.0;
}

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

// Docs: docs/backend/tests/t03-no-streamon.md
void run_no_streamon(const std::string &camera_path, MemoryBackend backend, TestResult &r, const LogFn &log,
                     const TestThresholds &tp) {
  const int buffer_count = static_cast<int>(tpv(tp, "t03-no-streamon", "buffer_count"));
  const int poll_timeout_ms = static_cast<int>(tpv(tp, "t03-no-streamon", "poll_timeout_ms"));
  emit(log, camera_path, "t01", "Opening device for STREAMON state check...");
  V4lSession s;
  std::string err;
  if (!s.open(camera_path, &err)) {
    r.status = TestStatus::Fail;
    r.summary = "Failed to open device: " + err;
    return;
  }
  if (!s.setup_buffers(buffer_count, backend, &err)) {
    r.status = TestStatus::Fail;
    r.summary = "VIDIOC_REQBUFS failed: " + err;
    return;
  }

  emit(log, camera_path, "t01",
       "Buffers allocated. Testing poll(" + std::to_string(poll_timeout_ms) + "ms) and DQBUF without STREAMON...");

  struct pollfd pfd;
  pfd.fd = s.fd();
  pfd.events = POLLIN;
  const int poll_ret = poll(&pfd, 1, poll_timeout_ms);

  struct v4l2_buffer buf;
  memset(&buf, 0, sizeof(buf));
  buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  buf.memory = V4L2_MEMORY_MMAP;
  const int dq_ret = ioctl(s.fd(), VIDIOC_DQBUF, &buf);
  const int dq_errno = errno;

  r.metrics.push_back(
      metric("poll_returned", "count", static_cast<double>(poll_ret),
             "poll(" + std::to_string(poll_timeout_ms) + "ms) return value without STREAMON; expected 0."));
  r.metrics.push_back(
      metric("dqbuf_failed", "bool", dq_ret < 0 ? 1.0 : 0.0, "Whether DQBUF correctly failed without STREAMON."));
  r.metrics.push_back(metric("dqbuf_errno", "errno", static_cast<double>(dq_errno),
                             "errno from DQBUF without STREAMON; expected EAGAIN(11) or EINVAL(22)."));

  r.details.push_back("poll(" + std::to_string(poll_timeout_ms) + "ms) returned " + std::to_string(poll_ret) +
                      " (expected 0)");
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

// Docs: docs/backend/tests/t07-buffer-overwrite.md
void run_buffer_overwrite(const std::string &camera_path, MemoryBackend backend, TriggerSource &trigger, TestResult &r,
                          const LogFn &log, const TestThresholds &th, const TestThresholds &tp) {
  const int VA_TRIGGERS = static_cast<int>(tpv(tp, "t07-buffer-overwrite", "variant_a_triggers"));
  const int VA_INTERVAL = static_cast<int>(tpv(tp, "t07-buffer-overwrite", "variant_a_interval_ms"));
  const int VB_TRIGGERS = static_cast<int>(tpv(tp, "t07-buffer-overwrite", "variant_b_triggers"));
  const int VB_INTERVAL = static_cast<int>(tpv(tp, "t07-buffer-overwrite", "variant_b_interval_ms"));
  const int SETTLE = static_cast<int>(tpv(tp, "t07-buffer-overwrite", "settle_ms"));
  const int BUF_COUNT = static_cast<int>(tpv(tp, "t07-buffer-overwrite", "buffer_count"));
  struct Variant {
    int triggers;
    int interval_ms;
    char key;
    const char *label;
  };
  const Variant variants[] = {
      {VA_TRIGGERS, VA_INTERVAL, 'A', "Variant A"},
      {VB_TRIGGERS, VB_INTERVAL, 'B', "Variant B"},
  };

  int error_frames_total = 0;
  for (const auto &v : variants) {
    emit(log, camera_path, "t02", std::string(v.label) + ": sending " + std::to_string(v.triggers) + " triggers...");
    V4lSession s;
    std::string err;
    if (!s.open(camera_path, &err) || !s.start(BUF_COUNT, backend, &err)) {
      r.status = TestStatus::Fail;
      r.summary = "Session setup failed: " + err;
      return;
    }
    V4lSession::sleep_ms(SETTLE);
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
  const int max_err = static_cast<int>(thv(th, "t07-buffer-overwrite", "max_error_flags"));
  if (error_frames_total > max_err) {
    r.status = TestStatus::Warn;
    r.summary =
        "Buffer saturation test completed; " + std::to_string(error_frames_total) + " frames had V4L2_BUF_FLAG_ERROR.";
  } else {
    r.status = TestStatus::Pass;
    r.summary = "Buffer saturation test completed. No error flags.";
  }
}

// Docs: docs/backend/tests/t13-trigger-latency.md
void run_trigger_latency(const std::string &camera_path, MemoryBackend backend, TriggerSource &trigger, TestResult &r,
                         const LogFn &log, const TestThresholds &tp) {
  const int SAMPLES = static_cast<int>(tpv(tp, "t13-trigger-latency", "sample_count"));
  const int warmup_count = static_cast<int>(tpv(tp, "t13-trigger-latency", "warmup_count"));
  const int capture_timeout_ms = static_cast<int>(tpv(tp, "t13-trigger-latency", "capture_timeout_ms"));
  const int sample_interval_ms = static_cast<int>(tpv(tp, "t13-trigger-latency", "sample_interval_ms"));
  V4lSession s;
  std::string err;
  if (!s.open(camera_path, &err) || !s.start(2, backend, &err)) {
    r.status = TestStatus::Fail;
    r.summary = "Session setup failed: " + err;
    return;
  }
  emit(log, camera_path, "t03", "Warming up camera (" + std::to_string(warmup_count) + " triggers)...");
  s.warmup(trigger, warmup_count);

  emit(log, camera_path, "t03",
       "Capturing " + std::to_string(SAMPLES) + " latency samples @ " + std::to_string(sample_interval_ms) +
           "ms interval...");
  std::vector<double> latencies;
  latencies.reserve(SAMPLES);
  int misses = 0;
  for (int i = 0; i < SAMPLES; i++) {
    auto f = s.capture(trigger, capture_timeout_ms);
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
    V4lSession::sleep_ms(sample_interval_ms);
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

// Docs: docs/backend/tests/t14-nonblock-vs-block.md
void run_nonblock_vs_block(const std::string &camera_path, MemoryBackend backend, TriggerSource &trigger, TestResult &r,
                           const LogFn &log, const TestThresholds &tp) {
  const int SAMPLES = static_cast<int>(tpv(tp, "t14-nonblock-vs-block", "sample_count"));
  const long spin_deadline_ms = static_cast<long>(tpv(tp, "t14-nonblock-vs-block", "spin_deadline_ms"));
  const int poll_timeout_ms = static_cast<int>(tpv(tp, "t14-nonblock-vs-block", "poll_timeout_ms"));
  const int sample_interval_ms = static_cast<int>(tpv(tp, "t14-nonblock-vs-block", "sample_interval_ms"));
  emit(log, camera_path, "t04",
       "Running NON_BLOCK vs BLOCK comparison (" + std::to_string(SAMPLES) + " samples each)...");

  std::vector<double> nb_lat;
  double avg_spins = 0.0;
  {
    V4lSession s;
    std::string err;
    if (!s.open(camera_path, &err) || !s.start(2, backend, &err)) {
      r.status = TestStatus::Fail;
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
      deadline.tv_nsec += spin_deadline_ms * 1'000'000L;
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
      V4lSession::sleep_ms(sample_interval_ms);
    }
    if (!spins.empty())
      avg_spins = std::accumulate(spins.begin(), spins.end(), 0.0) / spins.size();
  }

  std::vector<double> bl_lat;
  {
    V4lSession s;
    std::string err;
    if (!s.open(camera_path, &err)) {
      r.status = TestStatus::Fail;
      r.summary = "BLOCK session open failed: " + err;
      return;
    }
    int flags = fcntl(s.fd(), F_GETFL);
    fcntl(s.fd(), F_SETFL, flags & ~O_NONBLOCK);
    if (!s.start(2, backend, &err)) {
      r.status = TestStatus::Fail;
      r.summary = "BLOCK session start failed: " + err;
      return;
    }
    for (int i = 0; i < 5; i++) {
      trigger.send();
      struct pollfd pfd = {s.fd(), POLLIN, 0};
      if (poll(&pfd, 1, poll_timeout_ms) > 0) {
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
      V4lSession::sleep_ms(sample_interval_ms);
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

// Docs: docs/backend/tests/t16-format-comparison.md
void run_format_comparison(const std::string &camera_path, MemoryBackend backend, TriggerSource &trigger, TestResult &r,
                           const LogFn &log, const TestThresholds &tp) {
  const int SAMPLES = static_cast<int>(tpv(tp, "t16-format-comparison", "sample_count"));
  const int width = static_cast<int>(tpv(tp, "t16-format-comparison", "width"));
  const int height = static_cast<int>(tpv(tp, "t16-format-comparison", "height"));
  const int throughput_reps = static_cast<int>(tpv(tp, "t16-format-comparison", "throughput_reps"));
  static const char *fmts[] = {"YUYV", "UYVY"};
  emit(log, camera_path, "t06", "Format comparison: YUYV vs UYVY (" + std::to_string(SAMPLES) + " samples each)...");

  int ctrl_fd = ::open(camera_path.c_str(), O_RDWR | O_NONBLOCK);
  if (ctrl_fd < 0) {
    r.status = TestStatus::Fail;
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
    fmt.fmt.pix.width = width;
    fmt.fmt.pix.height = height;
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
            for (int rep = 0; rep < throughput_reps; rep++)
              memcpy(dst.data(), src, sz);
            clock_gettime(CLOCK_REALTIME, &t1);
            const double el = V4lSession::ts_diff_ms(t1, t0) / 1000.0;
            mbps = (static_cast<double>(sz) * throughput_reps / 1048576.0) / el;
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

// Docs: docs/backend/tests/t12-poll-timeout-cliff.md
// Adaptive cliff finder: binary search → fine sweep → stability confirmation.
void run_poll_timeout_cliff(const std::string &camera_path, MemoryBackend backend, TriggerSource &trigger,
                            TestResult &r, const LogFn &log, const TestThresholds &th, const TestThresholds &tp) {
  const int PROBE_FRAMES = static_cast<int>(tpv(tp, "t12-poll-timeout-cliff", "probe_frames"));
  const int STABILITY_ROUNDS = static_cast<int>(tpv(tp, "t12-poll-timeout-cliff", "stability_rounds"));
  const int STABILITY_FRAMES = static_cast<int>(tpv(tp, "t12-poll-timeout-cliff", "stability_frames"));
  const double PROD_MS = thv(th, "t12-poll-timeout-cliff", "production_timeout_ms");
  const double SAFE_MARGIN_THRESHOLD = thv(th, "t12-poll-timeout-cliff", "safe_margin_ms");
  emit(log, camera_path, "t12", "Poll timeout cliff finder: adaptive search...");

  V4lSession s;
  std::string err;
  if (!s.open(camera_path, &err) || !s.start(2, backend, &err)) {
    r.status = TestStatus::Fail;
    r.summary = "Session setup failed: " + err;
    return;
  }
  s.warmup(trigger, 10, 200);

  // Helper: probe a timeout value, return hit count out of PROBE_FRAMES
  auto probe = [&](int tms) -> int {
    int hits = 0;
    for (int i = 0; i < PROBE_FRAMES; i++) {
      auto f = s.capture(trigger, tms);
      if (f.success)
        hits++;
      V4lSession::sleep_ms(200);
    }
    return hits;
  };

  // Phase 1: Coarse binary search to find the cliff region.
  // Invariant: lo_ms is always 100% hit, hi_ms always has misses (or is 0).
  int lo_ms = -1, hi_ms = -1;

  // First, confirm top-end works (200ms should be miss-free).
  if (probe(200) < PROBE_FRAMES) {
    // Even 200ms misses — try 500ms
    if (probe(500) < PROBE_FRAMES) {
      // Pipeline cannot deliver even at 500ms — immediate Fail
      r.status = TestStatus::Fail;
      r.summary = "Misses even at 500ms timeout — no cliff to find.";
      r.metrics.push_back(metric("cliff_ms", "ms", -1.0, "Could not establish clean baseline."));
      r.metrics.push_back(metric("safety_margin_ms", "ms", -500.0, "N/A."));
      return;
    }
    lo_ms = 500;
  } else {
    lo_ms = 200;
  }

  // Find a hi_ms that misses by stepping down quickly
  static const int coarse_steps[] = {150, 100, 80, 60, 50, 40, 30, 20, 15, 10, 7, 5, 3, 2, 1};
  for (int step_ms : coarse_steps) {
    int hits = probe(step_ms);
    char buf[80];
    snprintf(buf, sizeof(buf), "  coarse: %3dms → %d/%d", step_ms, hits, PROBE_FRAMES);
    emit(log, camera_path, "t12", std::string(buf));
    r.details.push_back(std::string(buf + 2));
    if (hits < PROBE_FRAMES) {
      hi_ms = step_ms;
      break;
    }
    lo_ms = step_ms;  // Still clean — lower the clean bound
  }

  if (hi_ms < 0) {
    // No timeout in range caused misses
    r.metrics.push_back(metric("cliff_ms", "ms", -1.0, "No cliff found — pipeline reliable even at 1ms."));
    r.metrics.push_back(metric("safety_margin_ms", "ms", PROD_MS - 1.0, "Margin vs production timeout."));
    r.metrics.push_back(metric("stability_confirmed", "bool", 1.0, "N/A when no cliff."));
    r.status = TestStatus::Pass;
    r.summary = "No cliff found; reliable at all tested timeouts. Safety margin: " +
                std::to_string(static_cast<int>(PROD_MS - 1.0)) + "ms.";
    return;
  }

  // Phase 2: Binary search between lo_ms (clean) and hi_ms (has misses)
  emit(log, camera_path, "t12",
       "  binary search: lo=" + std::to_string(lo_ms) + "ms, hi=" + std::to_string(hi_ms) + "ms");
  while (lo_ms - hi_ms > 1) {
    int mid = (lo_ms + hi_ms) / 2;
    int hits = probe(mid);
    char buf[80];
    snprintf(buf, sizeof(buf), "  bsearch: %3dms → %d/%d", mid, hits, PROBE_FRAMES);
    emit(log, camera_path, "t12", std::string(buf));
    r.details.push_back(std::string(buf + 2));
    if (hits == PROBE_FRAMES)
      lo_ms = mid;
    else
      hi_ms = mid;
  }

  int cliff_candidate = lo_ms;  // Lowest timeout with 100% hits
  emit(log, camera_path, "t12", "  cliff candidate: " + std::to_string(cliff_candidate) + "ms");

  // Phase 3: Stability tracking — confirm the cliff is stable over multiple rounds
  int stable_rounds = 0;
  for (int round = 0; round < STABILITY_ROUNDS; round++) {
    V4lSession::sleep_ms(500);  // Brief pause between rounds
    int hits_at_cliff = 0;
    int hits_below = 0;
    for (int i = 0; i < STABILITY_FRAMES; i++) {
      auto f = s.capture(trigger, cliff_candidate);
      if (f.success)
        hits_at_cliff++;
      V4lSession::sleep_ms(200);
    }
    // Also check one below the cliff
    int below = cliff_candidate - 1;
    if (below >= 1) {
      for (int i = 0; i < STABILITY_FRAMES; i++) {
        auto f = s.capture(trigger, below);
        if (f.success)
          hits_below++;
        V4lSession::sleep_ms(200);
      }
    }
    bool round_ok = (hits_at_cliff == STABILITY_FRAMES) && (below < 1 || hits_below < STABILITY_FRAMES);
    if (round_ok)
      stable_rounds++;

    char buf[120];
    snprintf(buf, sizeof(buf), "  stability round %d: @%dms=%d/%d, @%dms=%d/%d %s", round + 1, cliff_candidate,
             hits_at_cliff, STABILITY_FRAMES, below, hits_below, STABILITY_FRAMES, round_ok ? "✓" : "✖");
    emit(log, camera_path, "t12", std::string(buf));
    r.details.push_back(std::string(buf + 2));
  }

  const int min_stable_rounds = (STABILITY_ROUNDS / 2) + 1;  // Majority of configured rounds must confirm the cliff
  bool stable = (stable_rounds >= min_stable_rounds);
  double safety = PROD_MS - static_cast<double>(cliff_candidate);

  // Push metrics
  r.metrics.push_back(metric("cliff_ms", "ms", static_cast<double>(cliff_candidate), "Stable cliff timeout."));
  r.metrics.push_back(metric("first_miss_ms", "ms", static_cast<double>(hi_ms), "Highest timeout with misses."));
  r.metrics.push_back(metric("safety_margin_ms", "ms", safety, "Production timeout - cliff timeout."));
  r.metrics.push_back(metric("stability_confirmed", "bool", stable ? 1.0 : 0.0,
                             "Whether cliff was stable over " + std::to_string(STABILITY_ROUNDS) + " rounds."));
  r.metrics.push_back(metric("stability_rounds_passed", "count", static_cast<double>(stable_rounds),
                             "Rounds that confirmed the cliff (out of " + std::to_string(STABILITY_ROUNDS) + ")."));

  // Emit summary box
  {
    char r0[64], r1[64], r2[64], r3[64], r4[64];
    snprintf(r0, sizeof(r0), "║  Production timeout : %5.1fms  ║", PROD_MS);
    snprintf(r1, sizeof(r1), "║  Cliff (stable)     : %5dms  ║", cliff_candidate);
    snprintf(r2, sizeof(r2), "║  Safety margin      : %5.1fms  ║", safety);
    snprintf(r3, sizeof(r3), "║  Stability          :   %d/%d    ║", stable_rounds, STABILITY_ROUNDS);
    snprintf(r4, sizeof(r4), "║  Confirmed          :   %s     ║", stable ? "YES" : "NO ");
    std::string box;
    box += "╔═══════ CLIFF SUMMARY ════════╗\n";
    box += std::string(r0) + "\n" + std::string(r1) + "\n" + std::string(r2) + "\n";
    box += std::string(r3) + "\n" + std::string(r4) + "\n";
    box += "╚══════════════════════════════╝";
    emit_data(log, camera_path, "t12", box);
  }

  // Verdict
  if (!stable) {
    r.status = TestStatus::Warn;
    r.summary = "Cliff at " + std::to_string(cliff_candidate) + "ms but unstable (" + std::to_string(stable_rounds) +
                "/" + std::to_string(STABILITY_ROUNDS) + " rounds confirmed).";
  } else if (safety >= SAFE_MARGIN_THRESHOLD) {
    r.status = TestStatus::Pass;
    r.summary = "Stable cliff at " + std::to_string(cliff_candidate) + "ms; " +
                std::to_string(static_cast<int>(safety)) + "ms safety margin.";
  } else if (safety >= 0.0) {
    r.status = TestStatus::Warn;
    r.summary = "Cliff at " + std::to_string(cliff_candidate) + "ms; only " + std::to_string(static_cast<int>(safety)) +
                "ms margin (threshold: " + std::to_string(static_cast<int>(SAFE_MARGIN_THRESHOLD)) + "ms).";
  } else {
    r.status = TestStatus::Fail;
    r.summary = "Cliff (" + std::to_string(cliff_candidate) + "ms) above production timeout (" +
                std::to_string(static_cast<int>(PROD_MS)) + "ms).";
  }
}

// Docs: docs/backend/tests/t19-sequence-continuity.md
void run_sequence_continuity(const std::string &camera_path, MemoryBackend backend, TriggerSource &trigger,
                             TestResult &r, const LogFn &log, const TestThresholds &th, const TestThresholds &tp) {
  const int NUM = static_cast<int>(tpv(tp, "t19-sequence-continuity", "sample_count"));
  const int capture_timeout_ms = static_cast<int>(tpv(tp, "t19-sequence-continuity", "capture_timeout_ms"));
  const int sample_interval_ms = static_cast<int>(tpv(tp, "t19-sequence-continuity", "sample_interval_ms"));
  emit(log, camera_path, "t08", "Sequence continuity: capturing " + std::to_string(NUM) + " frames...");
  V4lSession s;
  std::string err;
  if (!s.open(camera_path, &err) || !s.start(2, backend, &err)) {
    r.status = TestStatus::Fail;
    r.summary = "Session setup failed: " + err;
    return;
  }
  s.warmup(trigger);

  std::vector<uint32_t> seqs;
  std::vector<double> ts_us;
  seqs.reserve(NUM);
  ts_us.reserve(NUM);
  for (int i = 0; i < NUM; i++) {
    auto f = s.capture(trigger, capture_timeout_ms);
    if (f.success) {
      seqs.push_back(f.sequence);
      ts_us.push_back(f.timestamp.tv_sec * 1e6 + f.timestamp.tv_usec);
    }
    V4lSession::sleep_ms(sample_interval_ms);
  }

  if (seqs.size() < 2) {
    r.status = TestStatus::Fail;
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
    const int max_drop = static_cast<int>(thv(th, "t19-sequence-continuity", "max_dropped_frames"));
    const int max_nm = static_cast<int>(thv(th, "t19-sequence-continuity", "max_non_monotonic"));
    r.status = (total_gaps > max_drop || ts_non_mono > max_nm) ? TestStatus::Fail : TestStatus::Warn;
    r.summary = "dropped=" + std::to_string(total_gaps) + " dup=" + std::to_string(duplicates) +
                " non_mono=" + std::to_string(ts_non_mono) + ".";
  }
}

// Docs: docs/backend/tests/t22-sustained-capture.md
void run_sustained_capture(const std::string &camera_path, MemoryBackend backend, TriggerSource &trigger, TestResult &r,
                           const LogFn &log, const TestThresholds &th, const TestThresholds &tp) {
  const int DURATION_SEC = static_cast<int>(tpv(tp, "t22-sustained-capture", "duration_sec"));
  const int WINDOW_SEC = static_cast<int>(tpv(tp, "t22-sustained-capture", "window_sec"));
  const int INTERVAL_MS = static_cast<int>(tpv(tp, "t22-sustained-capture", "sample_interval_ms"));
  const int CAPTURE_TIMEOUT_MS = static_cast<int>(tpv(tp, "t22-sustained-capture", "capture_timeout_ms"));
  emit(log, camera_path, "t09",
       "Sustained capture: " + std::to_string(DURATION_SEC) + " seconds @ windows of " + std::to_string(WINDOW_SEC) +
           "s...");

  V4lSession s;
  std::string err;
  if (!s.open(camera_path, &err) || !s.start(2, backend, &err)) {
    r.status = TestStatus::Fail;
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
    auto f = s.capture(trigger, CAPTURE_TIMEOUT_MS);
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

  const double pass_rate = thv(th, "t22-sustained-capture", "pass_rate_pct");
  const double warn_rate = thv(th, "t22-sustained-capture", "warn_rate_pct");
  const double pass_drift = thv(th, "t22-sustained-capture", "pass_drift_ms");
  const double warn_drift = thv(th, "t22-sustained-capture", "warn_drift_ms");
  if (rate >= pass_rate && std::abs(drift) < pass_drift)
    r.status = TestStatus::Pass;
  else if (rate >= warn_rate && std::abs(drift) < warn_drift)
    r.status = TestStatus::Warn;
  else
    r.status = TestStatus::Fail;
  r.summary = std::to_string(static_cast<int>(all_lat.size())) + "/" + std::to_string(total) + " frames (" +
              std::to_string(static_cast<int>(rate)) + "%) drift=" + std::to_string(drift) + "ms.";
}

// Docs: docs/backend/tests/t06-multi-buffer.md
void run_multi_buffer(const std::string &camera_path, MemoryBackend backend, TriggerSource *trigger, TestResult &r,
                      const LogFn &log, const TestThresholds &tp) {
  const int SAMPLES = static_cast<int>(tpv(tp, "t06-multi-buffer", "sample_count"));
  const int MAX_BUFFERS = static_cast<int>(tpv(tp, "t06-multi-buffer", "max_buffers"));
  const int WARMUP_COUNT = static_cast<int>(tpv(tp, "t06-multi-buffer", "warmup_count"));
  const int CAPTURE_TIMEOUT_MS = static_cast<int>(tpv(tp, "t06-multi-buffer", "capture_timeout_ms"));
  const int SAMPLE_INTERVAL_MS = static_cast<int>(tpv(tp, "t06-multi-buffer", "sample_interval_ms"));
  emit(log, camera_path, "t10",
       "Multi-buffer configurations: testing 1-" + std::to_string(MAX_BUFFERS) + " buffers...");

  for (int bc = 1; bc <= MAX_BUFFERS; bc++) {
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
      s.warmup(*trigger, WARMUP_COUNT, SAMPLE_INTERVAL_MS);
      std::vector<double> lat;
      int misses = 0;
      for (int i = 0; i < SAMPLES; i++) {
        auto f = s.capture(*trigger, CAPTURE_TIMEOUT_MS);
        if (f.success)
          lat.push_back(f.latency_ms);
        else
          misses++;
        V4lSession::sleep_ms(SAMPLE_INTERVAL_MS);
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

// Docs: docs/backend/tests/t08-buffer-recycling.md
void run_buffer_recycling(const std::string &camera_path, MemoryBackend backend, TriggerSource &trigger, TestResult &r,
                          const LogFn &log, const TestThresholds &th, const TestThresholds &tp) {
  static const int delays_ms[] = {0, 1, 5, 10, 20, 30, 40, 48, 50, 60, 80, 100};
  constexpr int N = static_cast<int>(sizeof(delays_ms) / sizeof(delays_ms[0]));
  const int REPS = static_cast<int>(tpv(tp, "t08-buffer-recycling", "reps_per_delay"));
  const int CAPTURE_TIMEOUT_MS = static_cast<int>(tpv(tp, "t08-buffer-recycling", "capture_timeout_ms"));
  const int INTER_REP_INTERVAL_MS = static_cast<int>(tpv(tp, "t08-buffer-recycling", "inter_rep_interval_ms"));
  emit(log, camera_path, "t11",
       "Buffer recycling: testing " + std::to_string(N) + " delay values x " + std::to_string(REPS) + " reps...");

  V4lSession s;
  std::string err;
  if (!s.open(camera_path, &err) || !s.start(2, backend, &err)) {
    r.status = TestStatus::Fail;
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
      auto f1 = s.capture(trigger, CAPTURE_TIMEOUT_MS, true, false);
      if (!f1.success)
        continue;
      if (delay > 0)
        V4lSession::sleep_ms(delay);
      s.requeue(f1.index);
      auto f2 = s.capture(trigger, CAPTURE_TIMEOUT_MS, false, true);
      if (f2.success) {
        hits++;
        lat.push_back(f2.latency_ms);
      }
      V4lSession::sleep_ms(INTER_REP_INTERVAL_MS);
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
    const int min_safe = static_cast<int>(thv(th, "t08-buffer-recycling", "min_safe_cliff_delay_ms"));
    r.status = cliff_delay >= min_safe ? TestStatus::Pass : TestStatus::Warn;
    r.summary = "Buffer recycling cliff at " + std::to_string(cliff_delay) + "ms delay.";
  }
}

// Docs: docs/backend/tests/t05-stream-cycles.md
void run_stream_cycles(const std::string &camera_path, MemoryBackend backend, TriggerSource &trigger, TestResult &r,
                       const LogFn &log, const TestThresholds &th, const TestThresholds &tp) {
  const int FULL = static_cast<int>(tpv(tp, "t05-stream-cycles", "full_cycles"));
  const int RAPID = static_cast<int>(tpv(tp, "t05-stream-cycles", "rapid_cycles"));
  const int FULL_WARMUP = static_cast<int>(tpv(tp, "t05-stream-cycles", "full_warmup"));
  const int FULL_CAPTURES = static_cast<int>(tpv(tp, "t05-stream-cycles", "full_captures"));
  const int RAPID_PACING_MS = static_cast<int>(tpv(tp, "t05-stream-cycles", "rapid_pacing_ms"));
  int full_failures = 0;
  std::vector<double> first_lat;
  emit(log, camera_path, "t12",
       "STREAMON/STREAMOFF cycles: " + std::to_string(FULL) + " full + " + std::to_string(RAPID) + " rapid...");

  for (int c = 0; c < FULL; c++) {
    V4lSession s;
    std::string err;
    if (!s.open(camera_path, &err) || !s.start(2, backend, &err)) {
      full_failures++;
      continue;
    }
    s.warmup(trigger, FULL_WARMUP, 200);
    int ok = 0;
    for (int i = 0; i < FULL_CAPTURES; i++) {
      auto f = s.capture(trigger, 100);
      if (f.success) {
        ok++;
        if (i == 0)
          first_lat.push_back(f.latency_ms);
      }
      V4lSession::sleep_ms(100);
    }
    if (ok < FULL_CAPTURES)
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
    V4lSession::sleep_ms(RAPID_PACING_MS);
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
  const int max_fail_pass = static_cast<int>(thv(th, "t05-stream-cycles", "max_full_failures_pass"));
  const int max_fail_warn = static_cast<int>(thv(th, "t05-stream-cycles", "max_full_failures_warn"));
  const double rapid_pass = thv(th, "t05-stream-cycles", "rapid_pct_pass");
  const double rapid_warn = thv(th, "t05-stream-cycles", "rapid_pct_warn");
  if (full_failures <= max_fail_pass && rapid_pct >= rapid_pass)
    r.status = TestStatus::Pass;
  else if (full_failures <= max_fail_warn && rapid_pct >= rapid_warn)
    r.status = TestStatus::Warn;
  else
    r.status = TestStatus::Fail;
  r.summary = "Full: " + std::to_string(FULL - full_failures) + "/" + std::to_string(FULL) +
              " OK. Rapid: " + std::to_string(rapid_ok) + "/" + std::to_string(RAPID) + " captured.";
}

// Docs: docs/backend/tests/t09-buffer-flags.md
void run_buffer_flags(const std::string &camera_path, MemoryBackend backend, TriggerSource &trigger, TestResult &r,
                      const LogFn &log, const TestThresholds &th, const TestThresholds &tp) {
  const int NUM = static_cast<int>(tpv(tp, "t09-buffer-flags", "sample_count"));
  const int CAPTURE_TIMEOUT_MS = static_cast<int>(tpv(tp, "t09-buffer-flags", "capture_timeout_ms"));
  const int SAMPLE_INTERVAL_MS = static_cast<int>(tpv(tp, "t09-buffer-flags", "sample_interval_ms"));
  emit(log, camera_path, "t13", "Buffer flag analysis: capturing " + std::to_string(NUM) + " frames...");
  V4lSession s;
  std::string err;
  if (!s.open(camera_path, &err) || !s.start(2, backend, &err)) {
    r.status = TestStatus::Fail;
    r.summary = "Session setup failed: " + err;
    return;
  }
  s.warmup(trigger);

  int flag_error = 0, flag_keyframe = 0, flag_ts_mono = 0, flag_ts_copy = 0, flag_soe = 0, flag_eof = 0;
  uint32_t all_or = 0;
  int captured = 0;

  for (int i = 0; i < NUM; i++) {
    auto f = s.capture(trigger, CAPTURE_TIMEOUT_MS);
    if (!f.success) {
      V4lSession::sleep_ms(SAMPLE_INTERVAL_MS);
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
    V4lSession::sleep_ms(SAMPLE_INTERVAL_MS);
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

  if (flag_error > static_cast<int>(thv(th, "t09-buffer-flags", "max_error_flags"))) {
    r.status = TestStatus::Warn;
    r.summary = std::to_string(flag_error) + " frames with V4L2_BUF_FLAG_ERROR.";
  } else {
    r.status = TestStatus::Pass;
    r.summary = "No error flags. Timestamp source " + std::string(ts_ok ? "consistent." : "inconsistent.");
  }
}

// Docs: docs/backend/tests/t20-timestamp-monotonicity.md
void run_timestamp_monotonicity(const std::string &camera_path, MemoryBackend backend, TriggerSource &trigger,
                                TestResult &r, const LogFn &log, const TestThresholds &th, const TestThresholds &tp) {
  const int NUM = static_cast<int>(tpv(tp, "t20-timestamp-monotonicity", "sample_count"));
  const int CAPTURE_TIMEOUT_MS = static_cast<int>(tpv(tp, "t20-timestamp-monotonicity", "capture_timeout_ms"));
  const int SAMPLE_INTERVAL_MS = static_cast<int>(tpv(tp, "t20-timestamp-monotonicity", "sample_interval_ms"));
  emit(log, camera_path, "t14", "Timestamp monotonicity: capturing " + std::to_string(NUM) + " frames...");
  V4lSession s;
  std::string err;
  if (!s.open(camera_path, &err) || !s.start(2, backend, &err)) {
    r.status = TestStatus::Fail;
    r.summary = "Session setup failed: " + err;
    return;
  }
  s.warmup(trigger);

  std::vector<double> buf_ts, wall_ts;
  for (int i = 0; i < NUM; i++) {
    auto f = s.capture(trigger, CAPTURE_TIMEOUT_MS);
    if (f.success) {
      buf_ts.push_back(f.timestamp.tv_sec * 1e6 + f.timestamp.tv_usec);
      wall_ts.push_back(f.t_recv.tv_sec * 1e6 + f.t_recv.tv_nsec / 1000.0);
    }
    V4lSession::sleep_ms(SAMPLE_INTERVAL_MS);
  }

  if (buf_ts.size() < 2) {
    r.status = TestStatus::Fail;
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

  const int max_nm = static_cast<int>(thv(th, "t20-timestamp-monotonicity", "max_non_monotonic"));
  if (non_mono <= max_nm) {
    r.status = TestStatus::Pass;
    r.summary = "Timestamps monotonic across " + std::to_string(buf_ts.size()) + " frames.";
  } else {
    r.status = TestStatus::Fail;
    r.summary = std::to_string(non_mono) + " non-monotonic timestamps.";
  }
}

// Docs: docs/backend/tests/t10-memory-throughput.md
void run_memory_throughput(const std::string &camera_path, MemoryBackend backend, TestResult &r, const LogFn &log,
                           const TestThresholds &tp) {
  emit(log, camera_path, "t15", "Memory throughput benchmark: 100 reps per buffer type...");
  V4lSession s;
  std::string err;
  if (!s.open(camera_path, &err) || !s.setup_buffers(2, backend, &err)) {
    r.status = TestStatus::Fail;
    r.summary = "Buffer setup failed: " + err;
    return;
  }
  if (s.buffers().empty() || !s.buffers()[0].data()) {
    r.status = TestStatus::Fail;
    r.summary = "No mapped buffers available.";
    return;
  }

  const size_t frame_sz = s.buffers()[0].length;
  std::vector<uint8_t> dst(frame_sz);
  const int REPS = static_cast<int>(tpv(tp, "t10-memory-throughput", "benchmark_reps"));

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

// Docs: docs/backend/tests/t04-pollerr-handling.md
void run_pollerr_handling(const std::string &camera_path, MemoryBackend backend, TriggerSource &trigger, TestResult &r,
                          const LogFn &log, const TestThresholds &th, const TestThresholds &tp) {
  const int BASELINE_CAP = static_cast<int>(tpv(tp, "t04-pollerr-handling", "baseline_captures"));
  const int RECOVERY_CAP = static_cast<int>(tpv(tp, "t04-pollerr-handling", "recovery_captures"));
  const int POLL_TIMEOUT = static_cast<int>(tpv(tp, "t04-pollerr-handling", "poll_timeout_ms"));
  const int WARMUP = static_cast<int>(tpv(tp, "t04-pollerr-handling", "warmup_count"));
  emit(log, camera_path, "t17", "POLLERR/POLLHUP handling: testing STREAMOFF recovery...");
  V4lSession s;
  std::string err;
  if (!s.open(camera_path, &err) || !s.start(2, backend, &err)) {
    r.status = TestStatus::Fail;
    r.summary = "Session setup failed: " + err;
    return;
  }
  s.warmup(trigger, WARMUP, 200);

  int baseline_ok = 0;
  for (int i = 0; i < BASELINE_CAP; i++) {
    if (s.capture(trigger, POLL_TIMEOUT).success)
      baseline_ok++;
    V4lSession::sleep_ms(100);
  }
  s.streamoff();

  struct pollfd pfd = {s.fd(), POLLIN, 0};
  trigger.send();
  const int poll_ret = poll(&pfd, 1, POLL_TIMEOUT);
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
    s.warmup(trigger, WARMUP, 200);
    for (int i = 0; i < RECOVERY_CAP; i++) {
      if (s.capture(trigger, POLL_TIMEOUT).success)
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

  const int min_rec = static_cast<int>(thv(th, "t04-pollerr-handling", "min_recovery_ok"));
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

// Docs: docs/backend/tests/t11-dmabuf-cache-sync.md
void run_dmabuf_cache_sync(const std::string &camera_path, TriggerSource &trigger, TestResult &r, const LogFn &log,
                           const TestThresholds &th, const TestThresholds &tp) {
#if !V4L2DIAG_HAS_DMA_BUF_SYNC
  r.status = TestStatus::Skipped;
  r.summary = "linux/dma-buf.h not available; skipping cache coherency test.";
#else
  const int NUM = static_cast<int>(tpv(tp, "t11-dmabuf-cache-sync", "sample_count"));
  const size_t CMP = static_cast<size_t>(tpv(tp, "t11-dmabuf-cache-sync", "compare_bytes"));
  const int CAPTURE_TIMEOUT_MS = static_cast<int>(tpv(tp, "t11-dmabuf-cache-sync", "capture_timeout_ms"));
  emit(log, camera_path, "t18", "DMA_BUF_IOCTL_SYNC cache coherency: " + std::to_string(NUM) + " frames...");
  V4lSession s;
  std::string err;
  if (!s.open(camera_path, &err) || !s.start(2, MemoryBackend::Dmabuf, &err)) {
    r.status = TestStatus::Fail;
    r.summary = "DMABUF session failed: " + err;
    return;
  }
  s.warmup(trigger);

  int tested = 0, match_nosync = 0, match_sync = 0;
  for (int i = 0; i < NUM; i++) {
    auto f = s.capture(trigger, CAPTURE_TIMEOUT_MS, true, false);
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
    std::vector<uint8_t> mmap_d(CMP), nosync_d(CMP), sync_d(CMP);
    memcpy(mmap_d.data(), mp, CMP);
    memcpy(nosync_d.data(), dp, CMP);
    if (memcmp(mmap_d.data(), nosync_d.data(), CMP) == 0)
      match_nosync++;
    struct dma_buf_sync sync;
    sync.flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_READ;
    ioctl(dfd, DMA_BUF_IOCTL_SYNC, &sync);
    memcpy(sync_d.data(), dp, CMP);
    sync.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_READ;
    ioctl(dfd, DMA_BUF_IOCTL_SYNC, &sync);
    if (memcmp(mmap_d.data(), sync_d.data(), CMP) == 0)
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
    r.status = TestStatus::Fail;
    r.summary = "No frames compared.";
  } else {
    const double min_ratio = thv(th, "t11-dmabuf-cache-sync", "min_match_ratio");
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

// Docs: docs/backend/tests/t15-gpio-pulse-width.md
void run_gpio_pulse_width(const std::string &camera_path, MemoryBackend backend, TriggerSource &trigger, TestResult &r,
                          const LogFn &log, const TestThresholds &tp) {
  static const int pws[] = {1, 2, 3, 5, 7, 10, 13, 15, 20, 25, 30};
  constexpr int N = static_cast<int>(sizeof(pws) / sizeof(pws[0]));
  const int SAMPLES = static_cast<int>(tpv(tp, "t15-gpio-pulse-width", "samples_per_width"));
  const int WARMUP_COUNT = static_cast<int>(tpv(tp, "t15-gpio-pulse-width", "warmup_count"));
  const int POLL_TIMEOUT_MS = static_cast<int>(tpv(tp, "t15-gpio-pulse-width", "poll_timeout_ms"));
  emit(log, camera_path, "t19",
       "GPIO pulse width sweep: " + std::to_string(N) + " widths x " + std::to_string(SAMPLES) + " samples...");

  V4lSession s;
  std::string err;
  if (!s.open(camera_path, &err) || !s.start(2, backend, &err)) {
    r.status = TestStatus::Fail;
    r.summary = "Session setup failed: " + err;
    return;
  }
  s.warmup(trigger, WARMUP_COUNT, 200);

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
      if (poll(&pfd, 1, POLL_TIMEOUT_MS) > 0 && (pfd.revents & POLLIN)) {
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

// Docs: docs/backend/tests/t17-control-sweep.md
void run_control_sweep(const std::string &camera_path, TriggerSource &trigger, MemoryBackend backend, TestResult &r,
                       const LogFn &log, const TestThresholds &tp) {
  const int warmup_count = static_cast<int>(tpv(tp, "t17-control-sweep", "warmup_count"));
  const int sample_count = static_cast<int>(tpv(tp, "t17-control-sweep", "sample_count"));
  const int capture_timeout_ms = static_cast<int>(tpv(tp, "t17-control-sweep", "capture_timeout_ms"));
  emit(log, camera_path, "t20", "Camera control inventory + ISX021 sweep...");
  int fd = ::open(camera_path.c_str(), O_RDWR | O_NONBLOCK);
  if (fd < 0) {
    r.status = TestStatus::Fail;
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
        ss.warmup(trigger, warmup_count, capture_timeout_ms);
        std::vector<double> lats;
        for (int i = 0; i < sample_count; i++) {
          auto f = ss.capture(trigger, capture_timeout_ms);
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
void run_stuck_frame(const std::string &camera_path, MemoryBackend backend, TriggerSource &trigger, TestResult &r,
                     const LogFn &log, const TestThresholds &th, const TestThresholds &tp) {
  const int NUM = static_cast<int>(tpv(tp, "t21-stuck-frame", "sample_count"));
  const size_t CMP = static_cast<size_t>(tpv(tp, "t21-stuck-frame", "compare_bytes"));
  const int CAPTURE_TIMEOUT_MS = static_cast<int>(tpv(tp, "t21-stuck-frame", "capture_timeout_ms"));
  emit(log, camera_path, "t21", "Stuck frame detection: comparing " + std::to_string(NUM) + " consecutive frames...");

  V4lSession s;
  std::string err;
  if (!s.open(camera_path, &err) || !s.start(2, backend, &err)) {
    r.status = TestStatus::Fail;
    r.summary = "Session setup failed: " + err;
    return;
  }
  s.warmup(trigger);

  std::vector<uint8_t> prev(CMP, 0);
  int identical = 0, max_run = 0, cur_run = 0, tested = 0;

  for (int i = 0; i < NUM; i++) {
    auto f = s.capture(trigger, CAPTURE_TIMEOUT_MS, true, false);
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
    r.status = TestStatus::Fail;
    r.summary = "Insufficient frames.";
  } else if (identical == 0) {
    r.status = TestStatus::Pass;
    r.summary = "All " + std::to_string(tested) + " frames unique. No stuck frame.";
  } else if (max_run >= static_cast<int>(thv(th, "t21-stuck-frame", "max_identical_run"))) {
    r.status = TestStatus::Fail;
    r.summary = "Camera appears stuck: " + std::to_string(max_run) + " consecutive identical frames.";
  } else {
    r.status = TestStatus::Warn;
    r.summary = std::to_string(identical) + " identical pairs; max run=" + std::to_string(max_run) + ".";
  }
}

// Docs: docs/backend/tests/t23-latency-under-load.md
void run_latency_under_load(const std::string &camera_path, MemoryBackend backend, TriggerSource &trigger,
                            TestResult &r, const LogFn &log, const TestThresholds &th, const TestThresholds &tp) {
  const int SAMPLES = static_cast<int>(tpv(tp, "t23-latency-under-load", "sample_count"));
  const int LOAD_THREADS = static_cast<int>(tpv(tp, "t23-latency-under-load", "load_threads"));
  const int BASELINE_TIMEOUT_MS = static_cast<int>(tpv(tp, "t23-latency-under-load", "baseline_timeout_ms"));
  const int LOAD_TIMEOUT_MS = static_cast<int>(tpv(tp, "t23-latency-under-load", "load_timeout_ms"));
  const int SAMPLE_INTERVAL_MS = static_cast<int>(tpv(tp, "t23-latency-under-load", "sample_interval_ms"));
  emit(log, camera_path, "t22",
       "Latency under CPU load: baseline " + std::to_string(SAMPLES) + " samples, then " +
           std::to_string(LOAD_THREADS) + "-thread stress...");

  // Baseline
  std::vector<double> baseline;
  {
    V4lSession s;
    std::string err;
    if (!s.open(camera_path, &err) || !s.start(2, backend, &err)) {
      r.status = TestStatus::Fail;
      r.summary = "Baseline session failed: " + err;
      return;
    }
    s.warmup(trigger);
    for (int i = 0; i < SAMPLES; i++) {
      auto f = s.capture(trigger, BASELINE_TIMEOUT_MS);
      if (f.success)
        baseline.push_back(f.latency_ms);
      V4lSession::sleep_ms(SAMPLE_INTERVAL_MS);
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
        auto f = s.capture(trigger, LOAD_TIMEOUT_MS);
        if (f.success)
          under_load.push_back(f.latency_ms);
        V4lSession::sleep_ms(SAMPLE_INTERVAL_MS);
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

    if (dp95 < thv(th, "t23-latency-under-load", "pass_delta_p95_ms")) {
      r.status = TestStatus::Pass;
      r.summary = "CPU load impact minimal: p95 delta=" + std::to_string(dp95) + "ms.";
    } else if (dp95 < thv(th, "t23-latency-under-load", "warn_delta_p95_ms")) {
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

// Docs: docs/backend/tests/t02-control-inventory.md
void run_control_inventory(const std::string &camera_path, TestResult &r, const LogFn &log) {
  emit(log, camera_path, "t24", "Enumerating V4L2 controls...");
  int fd = ::open(camera_path.c_str(), O_RDWR | O_NONBLOCK);
  if (fd < 0) {
    r.status = TestStatus::Fail;
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

// Docs: docs/backend/tests/t18-resolution-sweep.md
void run_resolution_sweep(const std::string &camera_path, MemoryBackend backend, TriggerSource &trigger, TestResult &r,
                          const LogFn &log, const TestThresholds &tp) {
  const int SAMPLES = static_cast<int>(tpv(tp, "t18-resolution-sweep", "sample_count"));
  const int THROUGHPUT_REPS = static_cast<int>(tpv(tp, "t18-resolution-sweep", "throughput_reps"));
  emit(log, camera_path, "t18", "Resolution sweep: enumerating frame sizes and measuring...");

  int fd = ::open(camera_path.c_str(), O_RDWR | O_NONBLOCK);
  if (fd < 0) {
    r.status = TestStatus::Fail;
    r.summary = "Cannot open device: " + std::string(strerror(errno));
    return;
  }

  // Save original format
  struct v4l2_format orig_fmt {};
  orig_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  ioctl(fd, VIDIOC_G_FMT, &orig_fmt);

  // Enumerate frame sizes for the current pixel format
  struct v4l2_frmsizeenum frmsize {};
  frmsize.pixel_format = orig_fmt.fmt.pix.pixelformat;
  frmsize.index = 0;

  struct Resolution {
    uint32_t width;
    uint32_t height;
  };
  std::vector<Resolution> resolutions;

  while (ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &frmsize) == 0) {
    if (frmsize.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
      resolutions.push_back({frmsize.discrete.width, frmsize.discrete.height});
    } else if (frmsize.type == V4L2_FRMSIZE_TYPE_STEPWISE || frmsize.type == V4L2_FRMSIZE_TYPE_CONTINUOUS) {
      // Sample a few representative sizes from the range
      auto &sw = frmsize.stepwise;
      resolutions.push_back({sw.min_width, sw.min_height});
      uint32_t mid_w = (sw.min_width + sw.max_width) / 2;
      uint32_t mid_h = (sw.min_height + sw.max_height) / 2;
      resolutions.push_back({mid_w, mid_h});
      resolutions.push_back({sw.max_width, sw.max_height});
      break;
    }
    frmsize.index++;
  }

  if (resolutions.empty()) {
    ::close(fd);
    r.status = TestStatus::Warn;
    r.summary = "Device does not enumerate any frame sizes for format " + fourcc_to_string(frmsize.pixel_format) + ".";
    return;
  }

  r.metrics.push_back(
      metric("resolution_count", "count", static_cast<double>(resolutions.size()), "Resolutions tested."));

  int tested = 0;
  for (const auto &res : resolutions) {
    std::string label = std::to_string(res.width) + "x" + std::to_string(res.height);

    // Set format
    struct v4l2_format fmt {};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = res.width;
    fmt.fmt.pix.height = res.height;
    fmt.fmt.pix.pixelformat = orig_fmt.fmt.pix.pixelformat;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;
    if (ioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
      r.details.push_back(label + ": S_FMT failed — skipped");
      continue;
    }

    // Open a fresh session at this resolution
    V4lSession s;
    std::string err;
    if (!s.open(camera_path, &err) || !s.start(2, backend, &err)) {
      r.details.push_back(label + ": session failed — " + err);
      continue;
    }
    s.warmup(trigger, 3, 200);

    // Capture samples
    std::vector<double> latencies;
    for (int i = 0; i < SAMPLES; i++) {
      auto f = s.capture(trigger, 200);
      if (f.success)
        latencies.push_back(f.latency_ms);
      V4lSession::sleep_ms(200);
    }

    // Throughput on first buffer
    double throughput_mbps = 0.0;
    if (!s.buffers().empty() && s.buffers()[0].data()) {
      size_t sz = fmt.fmt.pix.sizeimage > 0 ? fmt.fmt.pix.sizeimage : s.buffers()[0].length;
      if (sz > 0) {
        std::vector<char> dst(sz);
        auto t0 = std::chrono::steady_clock::now();
        for (int rep = 0; rep < THROUGHPUT_REPS; rep++)
          memcpy(dst.data(), s.buffers()[0].data(), sz);
        auto t1 = std::chrono::steady_clock::now();
        double elapsed_s = std::chrono::duration<double>(t1 - t0).count();
        throughput_mbps = (static_cast<double>(sz) * THROUGHPUT_REPS / (1024.0 * 1024.0)) / elapsed_s;
      }
    }

    tested++;
    std::ostringstream detail;
    detail << label << ": ";
    if (!latencies.empty()) {
      Stats st = compute_stats(latencies);
      detail << "mean=" << static_cast<int>(st.mean) << "ms p95=" << static_cast<int>(st.p95) << "ms";
      r.metrics.push_back(metric(label + "_latency_mean", "ms", st.mean, "Mean latency at " + label));
      r.metrics.push_back(metric(label + "_latency_p95", "ms", st.p95, "P95 latency at " + label));
    } else {
      detail << "no frames captured";
    }
    detail << " throughput=" << static_cast<int>(throughput_mbps) << "MB/s";
    r.metrics.push_back(metric(label + "_throughput_mbps", "MB/s", throughput_mbps, "Memcpy throughput at " + label));
    r.details.push_back(detail.str());

    char line[128];
    snprintf(line, sizeof(line), "  %s: %zu/%d frames, %.0fMB/s", label.c_str(), latencies.size(), SAMPLES,
             throughput_mbps);
    emit(log, camera_path, "t18", std::string(line));
  }

  // Restore original format
  ioctl(fd, VIDIOC_S_FMT, &orig_fmt);
  ::close(fd);

  if (tested == 0) {
    r.status = TestStatus::Warn;
    r.summary = "Could not test any resolution (all S_FMT or session starts failed).";
  } else {
    r.status = TestStatus::Pass;
    r.summary = "Tested " + std::to_string(tested) + "/" + std::to_string(resolutions.size()) + " resolutions.";
  }
}

// Docs: docs/backend/tests/t26-cold-start.md
void run_cold_start(const std::string &camera_path, MemoryBackend backend, TriggerSource &trigger, TestResult &r,
                    const LogFn &log, const TestThresholds &tp) {
  const int CYCLES = static_cast<int>(tpv(tp, "t26-cold-start", "cycles"));
  const int MAX_FRAMES_PER_CYCLE = static_cast<int>(tpv(tp, "t26-cold-start", "max_frames_per_cycle"));
  const double STABILITY_THRESHOLD_PCT = tpv(tp, "t26-cold-start", "stability_threshold_pct");
  emit(log, camera_path, "t26", "Cold-start analysis: " + std::to_string(CYCLES) + " fresh cycles...");

  std::vector<int> warmup_counts;

  for (int cycle = 0; cycle < CYCLES; cycle++) {
    V4lSession s;
    std::string err;
    if (!s.open(camera_path, &err) || !s.start(2, backend, &err)) {
      r.details.push_back("cycle " + std::to_string(cycle + 1) + ": session failed — " + err);
      continue;
    }

    // Capture frames sequentially, tracking latency from the very first frame
    std::vector<double> latencies;
    for (int i = 0; i < MAX_FRAMES_PER_CYCLE; i++) {
      auto f = s.capture(trigger, 200);
      if (f.success)
        latencies.push_back(f.latency_ms);
      else
        latencies.push_back(-1.0);  // Mark miss
      V4lSession::sleep_ms(100);
    }

    // Find warmup point: first frame N where all subsequent frames up to end
    // are within STABILITY_THRESHOLD_PCT of their mean
    int warmup_frame = MAX_FRAMES_PER_CYCLE;  // default: never stabilized
    if (latencies.size() >= 5) {
      // Compute mean of last 5 frames as "steady state" reference
      double tail_sum = 0.0;
      int tail_count = 0;
      for (int i = static_cast<int>(latencies.size()) - 5; i < static_cast<int>(latencies.size()); i++) {
        if (latencies[i] > 0) {
          tail_sum += latencies[i];
          tail_count++;
        }
      }
      if (tail_count > 0) {
        double steady_mean = tail_sum / tail_count;
        double threshold = steady_mean * (STABILITY_THRESHOLD_PCT / 100.0);
        // Walk forward to find first frame within threshold of steady_mean
        // where all subsequent good frames also stay within threshold
        for (int start = 0; start < static_cast<int>(latencies.size()) - 2; start++) {
          if (latencies[start] <= 0)
            continue;
          bool all_stable = true;
          for (int j = start; j < static_cast<int>(latencies.size()); j++) {
            if (latencies[j] <= 0)
              continue;
            if (std::abs(latencies[j] - steady_mean) > threshold) {
              all_stable = false;
              break;
            }
          }
          if (all_stable) {
            warmup_frame = start;
            break;
          }
        }
      }
    }
    warmup_counts.push_back(warmup_frame);

    char line[80];
    snprintf(line, sizeof(line), "  cycle %2d: warmup=%d frames", cycle + 1, warmup_frame);
    emit(log, camera_path, "t26", std::string(line));
    r.details.push_back("cycle " + std::to_string(cycle + 1) + ": warmup=" + std::to_string(warmup_frame) + " frames");
  }

  if (warmup_counts.empty()) {
    r.status = TestStatus::Fail;
    r.summary = "All cycles failed to open a session.";
    return;
  }

  // Compute stats over warmup counts
  double sum = 0;
  int max_warmup = 0;
  for (int wc : warmup_counts) {
    sum += wc;
    max_warmup = std::max(max_warmup, wc);
  }
  double mean_warmup = sum / warmup_counts.size();

  r.metrics.push_back(metric("warmup_mean_frames", "count", mean_warmup, "Mean frames to reach steady-state latency."));
  r.metrics.push_back(
      metric("warmup_max_frames", "count", static_cast<double>(max_warmup), "Worst-case warmup frames."));
  r.metrics.push_back(
      metric("cycles_completed", "count", static_cast<double>(warmup_counts.size()), "Successful cycles."));

  if (mean_warmup <= 3.0) {
    r.status = TestStatus::Pass;
    r.summary = "Fast warm-up: mean=" + std::to_string(static_cast<int>(mean_warmup)) +
                " frames, max=" + std::to_string(max_warmup) + ".";
  } else if (mean_warmup <= 10.0) {
    r.status = TestStatus::Pass;
    r.summary = "Moderate warm-up: mean=" + std::to_string(static_cast<int>(mean_warmup)) +
                " frames, max=" + std::to_string(max_warmup) + ".";
  } else {
    r.status = TestStatus::Warn;
    r.summary = "Slow warm-up: mean=" + std::to_string(static_cast<int>(mean_warmup)) +
                " frames, max=" + std::to_string(max_warmup) + " — consider longer warm-up in other tests.";
  }
}

// Docs: docs/backend/tests/t24-max-fps.md
void run_max_fps(const std::string &camera_path, MemoryBackend backend, TriggerSource &trigger, TestResult &r,
                 const LogFn &log, const TestThresholds &tp) {
  const int DURATION_SEC = static_cast<int>(tpv(tp, "t24-max-fps", "duration_sec"));
  const int WARMUP_FRAMES = static_cast<int>(tpv(tp, "t24-max-fps", "warmup_frames"));
  const int POLL_TIMEOUT = static_cast<int>(tpv(tp, "t24-max-fps", "poll_timeout_ms"));
  emit(log, camera_path, "t24", "Max FPS: measuring sustained rate for " + std::to_string(DURATION_SEC) + "s...");

  V4lSession s;
  std::string err;
  if (!s.open(camera_path, &err) || !s.start(4, backend, &err)) {
    r.status = TestStatus::Fail;
    r.summary = "Session setup failed: " + err;
    return;
  }
  // Warmup
  for (int i = 0; i < WARMUP_FRAMES; i++) {
    s.capture(trigger, POLL_TIMEOUT);
  }

  emit(log, camera_path, "t24", "Warmup done, starting timed capture...");
  const auto t_start = std::chrono::steady_clock::now();
  const auto t_end = t_start + std::chrono::seconds(DURATION_SEC);
  int total_sent = 0, total_received = 0, total_missed = 0;
  std::vector<double> latencies;
  int window_frames = 0;
  double max_window_fps = 0.0;
  auto window_start = t_start;

  while (std::chrono::steady_clock::now() < t_end) {
    total_sent++;
    auto f = s.capture(trigger, POLL_TIMEOUT);
    if (f.success) {
      total_received++;
      window_frames++;
      latencies.push_back(f.latency_ms);
    } else {
      total_missed++;
    }
    // Calculate per-second window FPS
    auto now = std::chrono::steady_clock::now();
    double window_elapsed = std::chrono::duration<double>(now - window_start).count();
    if (window_elapsed >= 1.0) {
      double window_fps = window_frames / window_elapsed;
      max_window_fps = std::max(max_window_fps, window_fps);
      window_frames = 0;
      window_start = now;
    }
  }

  const auto actual_end = std::chrono::steady_clock::now();
  double total_elapsed = std::chrono::duration<double>(actual_end - t_start).count();
  double sustained_fps = total_received / total_elapsed;
  double send_rate = total_sent / total_elapsed;
  double drop_pct = total_sent > 0 ? (100.0 * total_missed / total_sent) : 0.0;

  r.metrics.push_back(metric("sustained_fps", "fps", sustained_fps, "Sustained frame rate over test duration."));
  r.metrics.push_back(metric("max_window_fps", "fps", max_window_fps, "Peak 1-second window frame rate."));
  r.metrics.push_back(metric("send_rate", "fps", send_rate, "Trigger send rate achieved."));
  r.metrics.push_back(metric("total_sent", "count", static_cast<double>(total_sent), "Triggers sent."));
  r.metrics.push_back(metric("total_received", "count", static_cast<double>(total_received), "Frames received."));
  r.metrics.push_back(metric("total_missed", "count", static_cast<double>(total_missed), "Frames missed."));
  r.metrics.push_back(metric("drop_pct", "%", drop_pct, "Frame drop percentage."));
  if (!latencies.empty()) {
    Stats ls = compute_stats(latencies);
    push_stats_metrics(r.metrics, "latency", ls);
  }

  r.details.push_back("Duration: " + std::to_string(static_cast<int>(total_elapsed)) + "s");
  r.details.push_back("Sustained FPS: " + std::to_string(static_cast<int>(sustained_fps)));
  r.details.push_back("Peak window FPS: " + std::to_string(static_cast<int>(max_window_fps)));
  r.details.push_back("Drop rate: " + std::to_string(static_cast<int>(drop_pct)) + "%");

  emit(log, camera_path, "t24",
       "Result: " + std::to_string(static_cast<int>(sustained_fps)) + " fps sustained, " +
           std::to_string(static_cast<int>(drop_pct)) + "% drops");

  if (drop_pct < 5.0) {
    r.status = TestStatus::Pass;
    r.summary = "Sustained " + std::to_string(static_cast<int>(sustained_fps)) + " fps with <5% drops.";
  } else if (drop_pct < 20.0) {
    r.status = TestStatus::Warn;
    r.summary = "Sustained " + std::to_string(static_cast<int>(sustained_fps)) + " fps but " +
                std::to_string(static_cast<int>(drop_pct)) + "% drops observed.";
  } else {
    r.status = TestStatus::Fail;
    r.summary = "High drop rate (" + std::to_string(static_cast<int>(drop_pct)) +
                "%) — pipeline cannot sustain the trigger rate.";
  }
}

// Docs: docs/backend/tests/t25-multi-camera.md
void run_multi_camera(const std::vector<std::string> &camera_paths, MemoryBackend backend, TriggerSource &trigger,
                      TestResult &r, const LogFn &log, const TestThresholds &tp) {
  const int SAMPLES = static_cast<int>(tpv(tp, "t25-multi-camera", "sample_count"));
  const int POLL_TIMEOUT = static_cast<int>(tpv(tp, "t25-multi-camera", "poll_timeout_ms"));
  emit(
      log, camera_paths[0], "t25",
      "Multi-camera: " + std::to_string(camera_paths.size()) + " devices x " + std::to_string(SAMPLES) + " samples...");

  // Open sessions on all cameras
  std::vector<V4lSession> sessions(camera_paths.size());
  for (size_t i = 0; i < camera_paths.size(); i++) {
    std::string err;
    if (!sessions[i].open(camera_paths[i], &err) || !sessions[i].start(2, backend, &err)) {
      r.status = TestStatus::Fail;
      r.summary = "Failed to open camera " + camera_paths[i] + ": " + err;
      return;
    }
  }
  // Warmup all
  for (auto &s : sessions)
    s.warmup(trigger);

  // Concurrent capture: drain all queues, fire ONE shared trigger, then poll
  // every camera against that single t_trigger so latency/jitter reflect
  // actual cross-device skew rather than each camera's own re-triggered pulse.
  std::vector<std::vector<double>> per_cam_latencies(camera_paths.size());
  std::vector<double> cross_jitters;
  int successful_rounds = 0;

  for (int sample = 0; sample < SAMPLES; sample++) {
    for (auto &s : sessions)
      s.drain();
    V4lSession::sleep_ms(10);

    const struct timespec t_trigger = trigger.send();

    std::vector<struct pollfd> pfds(sessions.size());
    for (size_t i = 0; i < sessions.size(); i++) {
      pfds[i].fd = sessions[i].fd();
      pfds[i].events = POLLIN;
      pfds[i].revents = 0;
    }
    poll(pfds.data(), pfds.size(), POLL_TIMEOUT);

    std::vector<double> round_latencies;
    bool all_ok = true;
    for (size_t i = 0; i < sessions.size(); i++) {
      double latency_ms = -1.0;
      if (pfds[i].revents & POLLIN) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        if (ioctl(sessions[i].fd(), VIDIOC_DQBUF, &buf) == 0) {
          struct timespec t_recv;
          clock_gettime(CLOCK_REALTIME, &t_recv);
          latency_ms = V4lSession::ts_diff_ms(t_recv, t_trigger);
          sessions[i].requeue(buf.index);
        }
      }
      if (latency_ms >= 0.0) {
        per_cam_latencies[i].push_back(latency_ms);
        round_latencies.push_back(latency_ms);
      } else {
        all_ok = false;
        per_cam_latencies[i].push_back(-1.0);
      }
    }
    if (all_ok && round_latencies.size() >= 2) {
      double min_l = *std::min_element(round_latencies.begin(), round_latencies.end());
      double max_l = *std::max_element(round_latencies.begin(), round_latencies.end());
      cross_jitters.push_back(max_l - min_l);
      successful_rounds++;
    }
    V4lSession::sleep_ms(100);
  }

  // Report per-camera stats
  for (size_t i = 0; i < camera_paths.size(); i++) {
    std::vector<double> valid;
    for (double v : per_cam_latencies[i])
      if (v > 0)
        valid.push_back(v);
    if (!valid.empty()) {
      Stats ls = compute_stats(valid);
      std::string prefix = "cam" + std::to_string(i) + "_latency";
      push_stats_metrics(r.metrics, prefix, ls);
    }
    r.details.push_back(camera_paths[i] + ": " + std::to_string(per_cam_latencies[i].size()) + " captures");
  }

  // Cross-camera jitter
  if (!cross_jitters.empty()) {
    Stats js = compute_stats(cross_jitters);
    push_stats_metrics(r.metrics, "cross_jitter", js);
    r.metrics.push_back(
        metric("successful_rounds", "count", static_cast<double>(successful_rounds), "Rounds all cameras captured."));
    r.details.push_back("Cross-camera jitter mean: " + std::to_string(js.mean) + " ms");

    if (js.p95 < 5.0) {
      r.status = TestStatus::Pass;
      r.summary = "Cross-device jitter p95=" + std::to_string(static_cast<int>(js.p95)) + "ms across " +
                  std::to_string(camera_paths.size()) + " cameras.";
    } else if (js.p95 < 20.0) {
      r.status = TestStatus::Warn;
      r.summary = "Moderate cross-device jitter p95=" + std::to_string(static_cast<int>(js.p95)) + "ms.";
    } else {
      r.status = TestStatus::Fail;
      r.summary = "High cross-device jitter p95=" + std::to_string(static_cast<int>(js.p95)) + "ms.";
    }
  } else {
    r.status = TestStatus::Fail;
    r.summary = "No successful round with all cameras capturing simultaneously.";
  }
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

const TestThresholds &DiagnosticRunner::params_for(const std::string &test_id) const {
  static const TestThresholds empty;
  auto it = active_thresholds_.params.find(test_id);
  return it != active_thresholds_.params.end() ? it->second : empty;
}

RunResult DiagnosticRunner::run(const RunConfig &config) {
  // Resolve threshold configuration once before any per-camera work.
  {
    // Thresholds live under the standard config root (…/v4l2-camera-diagnostic/
    // thresholds), a sibling of the profiles directory — independent of the
    // profiles --config-dir, which is ProfileRegistry's own directory.
    ThresholdRegistry registry(default_threshold_directory());
    active_thresholds_ = registry.resolve(config.threshold_config_id.empty() ? "default" : config.threshold_config_id);
  }
  multi_camera_claimed_ = false;

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

  if (definition.id == "t01-device-compliance") {
    // Layer 1: Discovery — merged old t16-v4l2-compliance + v4l2-memory-probe
    emit(log, camera_path, "t01", "V4L2 device compliance check...");
    DeviceInfo info;
    if (!query_device(camera_path, &info)) {
      result.status = TestStatus::Fail;
      result.summary = "Failed to query V4L2 device: " + info.error;
    } else {
      // Backend probe
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
      // Capabilities
      result.metrics.push_back(metric("supports_capture", "bool", info.supports_capture ? 1.0 : 0.0,
                                      "Whether the device reports V4L2 capture support."));
      result.metrics.push_back(metric("supports_streaming", "bool", info.supports_streaming ? 1.0 : 0.0,
                                      "Whether the device reports V4L2 streaming support."));
      result.metrics.push_back(metric("format_count", "count", static_cast<double>(info.formats.size()),
                                      "Number of enumerated V4L2 pixel formats."));
      result.metrics.push_back(metric("selected_backend_supported", "bool", selected_supported ? 1.0 : 0.0,
                                      "Whether the selected memory backend is accepted by VIDIOC_REQBUFS."));
      result.details.push_back("Driver: " + info.driver);
      result.details.push_back("Card: " + info.card);
      result.details.push_back("Bus: " + info.bus_info);
      result.details.push_back("Selected backend: " + std::string(to_string(backend)));
      for (const auto &fmt : info.formats)
        result.details.push_back("Format: " + fmt.fourcc + " (" + fmt.description + ", " + fmt.buffer_type + ")");
      // Verdict
      if (!info.supports_capture || !info.supports_streaming) {
        result.status = TestStatus::Fail;
        result.summary = "Device does not expose the required V4L2 capture and streaming capabilities.";
      } else if (!selected_supported) {
        result.status = TestStatus::Warn;
        result.summary =
            "Device supports capture/streaming but the selected memory backend was not accepted by VIDIOC_REQBUFS.";
      } else {
        result.status = TestStatus::Pass;
        result.summary = "Device exposes capture, streaming, and selected memory backend.";
      }
    }
  } else if (definition.id == "t02-control-inventory")
    run_control_inventory(camera_path, result, log);
  else if (definition.id == "t03-no-streamon")
    run_no_streamon(camera_path, backend, result, log, params_for(definition.id));
  else if (definition.id == "t04-pollerr-handling")
    run_pollerr_handling(camera_path, backend, *trigger, result, log, thresholds_for(definition.id),
                         params_for(definition.id));
  else if (definition.id == "t05-stream-cycles")
    run_stream_cycles(camera_path, backend, *trigger, result, log, thresholds_for(definition.id),
                      params_for(definition.id));
  else if (definition.id == "t06-multi-buffer")
    run_multi_buffer(camera_path, backend, trigger, result, log, params_for(definition.id));
  else if (definition.id == "t07-buffer-overwrite")
    run_buffer_overwrite(camera_path, backend, *trigger, result, log, thresholds_for(definition.id),
                         params_for(definition.id));
  else if (definition.id == "t08-buffer-recycling")
    run_buffer_recycling(camera_path, backend, *trigger, result, log, thresholds_for(definition.id),
                         params_for(definition.id));
  else if (definition.id == "t09-buffer-flags")
    run_buffer_flags(camera_path, backend, *trigger, result, log, thresholds_for(definition.id),
                     params_for(definition.id));
  else if (definition.id == "t10-memory-throughput")
    run_memory_throughput(camera_path, backend, result, log, params_for(definition.id));
  else if (definition.id == "t11-dmabuf-cache-sync")
    run_dmabuf_cache_sync(camera_path, *trigger, result, log, thresholds_for(definition.id), params_for(definition.id));
  else if (definition.id == "t12-poll-timeout-cliff")
    run_poll_timeout_cliff(camera_path, backend, *trigger, result, log, thresholds_for(definition.id),
                           params_for(definition.id));
  else if (definition.id == "t13-trigger-latency")
    run_trigger_latency(camera_path, backend, *trigger, result, log, params_for(definition.id));
  else if (definition.id == "t14-nonblock-vs-block")
    run_nonblock_vs_block(camera_path, backend, *trigger, result, log, params_for(definition.id));
  else if (definition.id == "t15-gpio-pulse-width")
    run_gpio_pulse_width(camera_path, backend, *trigger, result, log, params_for(definition.id));
  else if (definition.id == "t16-format-comparison")
    run_format_comparison(camera_path, backend, *trigger, result, log, params_for(definition.id));
  else if (definition.id == "t17-control-sweep")
    run_control_sweep(camera_path, *trigger, backend, result, log, params_for(definition.id));
  else if (definition.id == "t18-resolution-sweep") {
    run_resolution_sweep(camera_path, backend, *trigger, result, log, params_for(definition.id));
  } else if (definition.id == "t19-sequence-continuity")
    run_sequence_continuity(camera_path, backend, *trigger, result, log, thresholds_for(definition.id),
                            params_for(definition.id));
  else if (definition.id == "t20-timestamp-monotonicity")
    run_timestamp_monotonicity(camera_path, backend, *trigger, result, log, thresholds_for(definition.id),
                               params_for(definition.id));
  else if (definition.id == "t21-stuck-frame")
    run_stuck_frame(camera_path, backend, *trigger, result, log, thresholds_for(definition.id),
                    params_for(definition.id));
  else if (definition.id == "t22-sustained-capture")
    run_sustained_capture(camera_path, backend, *trigger, result, log, thresholds_for(definition.id),
                          params_for(definition.id));
  else if (definition.id == "t23-latency-under-load")
    run_latency_under_load(camera_path, backend, *trigger, result, log, thresholds_for(definition.id),
                           params_for(definition.id));
  else if (definition.id == "t24-max-fps") {
    run_max_fps(camera_path, backend, *trigger, result, log, params_for(definition.id));
  } else if (definition.id == "t25-multi-camera") {
    // Multi-camera requires multiple camera paths from config.cameras. It opens
    // every camera itself, so it must run at most once per run() — the first
    // camera/group to reach it claims multi_camera_claimed_ and runs it for the
    // whole camera set; every other dispatch (including concurrent ones under
    // RunMode::Parallel) is skipped rather than re-running or racing on the
    // same devices.
    if (config.cameras.size() <= 1) {
      result.status = TestStatus::Skipped;
      result.summary = "Multi-camera test requires 2+ cameras selected.";
    } else if (multi_camera_claimed_.exchange(true)) {
      result.status = TestStatus::Skipped;
      result.summary = "Multi-camera test already run once for this run (covers all cameras).";
    } else {
      std::vector<std::string> paths;
      for (const auto &cam : config.cameras)
        paths.push_back(cam.path);
      run_multi_camera(paths, backend, *trigger, result, log, params_for(definition.id));
    }
  } else if (definition.id == "t26-cold-start") {
    run_cold_start(camera_path, backend, *trigger, result, log, params_for(definition.id));
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
