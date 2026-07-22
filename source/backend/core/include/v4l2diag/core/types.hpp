#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace v4l2diag {

enum class MemoryBackend {
  Mmap,
  Dmabuf,
  UserPtr,
};

enum class ReportFormat {
  Json,
  Markdown,
  Html,
  Pdf,
};

enum class RunMode {
  Sequential,
  Parallel,
};

enum class TriggerMode {
  Hardware,
  Software,
  FreeRun,
};

enum class TestStatus {
  Pass,
  Fail,
  Warn,
  Skipped,
  Error,
};

struct MetricValue {
  std::string name;
  std::string unit;
  double value = 0.0;
  std::string description;
};

struct TestResult {
  std::string id;
  std::string name;
  std::string category;
  std::string memory_backend;
  TestStatus status = TestStatus::Skipped;
  std::string summary;
  std::vector<MetricValue> metrics;
  std::vector<std::string> details;
  std::vector<std::string> warnings;
  double duration_ms = 0.0;
};

struct CameraRunResult {
  std::string camera_path;
  std::string profile_id;
  std::string trigger_channel_id;
  std::string trigger_description;
  TriggerMode trigger_mode = TriggerMode::FreeRun;
  std::vector<MemoryBackend> memory_backends;
  std::vector<TestResult> tests;
};

struct RunResult {
  std::string project_name = "v4l2-camera-diagnostic";
  std::string started_at_utc;
  std::string finished_at_utc;
  std::string host_name;
  std::string output_directory;
  RunMode run_mode = RunMode::Sequential;
  std::vector<CameraRunResult> cameras;
};

struct RunConfig {
  struct CameraConfig {
    std::string path;
    std::string profile_id;
    std::string trigger_channel_id;
  };

  std::vector<CameraConfig> cameras;
  TriggerMode trigger_mode = TriggerMode::FreeRun;
  std::vector<MemoryBackend> memory_backends;
  std::vector<std::string> test_selectors;
  std::vector<ReportFormat> report_formats;
  std::string output_directory = "reports";
  std::string config_directory;
  // Name of the threshold configuration to apply when deciding Pass/Warn/Fail.
  // "default" (or empty) uses the built-in defaults; other ids resolve to a
  // user config in the threshold directory. See ThresholdRegistry.
  std::string threshold_config_id = "default";
  RunMode run_mode = RunMode::Sequential;
  bool include_long_tests = false;
  bool include_experimental_tests = false;

  // Optional callback invoked after each test completes. Useful for streaming
  // per-test progress to a web client without waiting for the full run.
  std::function<void(const std::string &camera_path, const TestResult &result)> progress_callback;

  // Optional fine-grained log callback fired during test execution. Unlike
  // progress_callback (which fires once per completed test), this fires many
  // times within a single test to provide real-time visibility into progress.
  // log_type: "section_start", "progress", "data", "summary"
  std::function<void(const std::string &severity, const std::string &camera, const std::string &test,
                     const std::string &message, const std::string &log_type)>
      log_callback;

  // Optional cancellation token. When set to true, the runner should abort ASAP.
  std::atomic<bool> *stop_token = nullptr;
};

const char *to_string(MemoryBackend backend);
const char *to_string(ReportFormat format);
const char *to_string(RunMode mode);
const char *to_string(TriggerMode mode);
const char *to_string(TestStatus status);

bool parse_memory_backend(const std::string &value, MemoryBackend *backend);
bool parse_report_format(const std::string &value, ReportFormat *format);
bool parse_run_mode(const std::string &value, RunMode *mode);
bool parse_trigger_mode(const std::string &value, TriggerMode *mode);

std::vector<std::string> split_csv(const std::string &value);
std::string trim(const std::string &value);
std::string utc_timestamp();
std::string host_name();

}  // namespace v4l2diag
