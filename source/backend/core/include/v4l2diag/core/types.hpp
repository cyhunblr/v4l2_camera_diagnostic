#pragma once

#include "v4l2diag/core/role_bindings.hpp"

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

// No Pdf member. The product does not generate PDFs; the HTML report carries an
// "Export as PDF" button that calls window.print(), i.e. exactly what Ctrl+P
// does. See docs/report-ui-review-plan.md §6.2.
enum class ReportFormat {
  Json,
  Markdown,
  Html,
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
  // Short data lines — "coarse: 150ms -> 10/10", "mmap_4k: 28722 MB/s". Rendered
  // as a monospace list, and machine-parsed in places, so keep them terse and
  // regular.
  std::vector<std::string> details;
  // Prose that explains what a result means or what to do about it. Rendered as
  // a callout in its own right, because an explanatory paragraph buried in the
  // monospace detail list reads as jargon to anyone who does not already know
  // the project.
  std::vector<std::string> notes;
  std::vector<std::string> warnings;
  double duration_ms = 0.0;
};

struct CameraRunResult {
  std::string camera_path;
  // The run role this camera played: "master" or "slave-N". Assigned by the runner
  // as it executes, never inferred from position by the report writer -- a reader
  // that recomputed it from the camera order would silently disagree the moment
  // the order changed.
  std::string role;
  // Kept per camera on purpose: this is the physical resolution -- which GPIO line
  // or which control this camera ended up on -- not a routing decision restated.
  // trigger_mode and the timing are run-level; see RunResult.
  std::string trigger_description;
  std::vector<MemoryBackend> memory_backends;
  std::vector<TestResult> tests;
};

struct RunResult {
  std::string project_name = "v4l2-camera-diagnostic";
  std::string started_at_utc;
  std::string finished_at_utc;
  std::string host_name;
  std::string kernel_release;
  std::string kernel_version;
  std::string output_directory;
  RunMode run_mode = RunMode::Sequential;

  // --- Run-level trigger contract (plan 2.5, report model (a)) -------------
  //
  // Stated once here rather than repeated per camera. A camera's channel is found
  // by taking its `role` and looking it up in `role_bindings` below.
  TriggerMode trigger_mode = TriggerMode::FreeRun;
  // Empty for free-run, which routes nothing.
  std::string trigger_profile_id;
  // The validated routing, in topology order. Empty for free-run.
  std::vector<RoleBinding> role_bindings;
  // Trigger timing for the run, filled once by the runner rather than derived from
  // cameras.front() by each consumer.
  //
  // These always hold a value; under free-run it is the struct default, which is
  // MEANINGLESS -- nothing is driven, so there is no rate. The contract is that
  // consumers do not RENDER the fields when trigger_mode is FreeRun, not that the
  // fields are absent. Making absence representable would need an optional model;
  // until then the guard lives in each consumer, and the report/API tests check
  // that the fields do not appear in free-run output.
  double trigger_rate_hz = 30.0;
  double pulse_width_ms = 13.0;

  // Source file names for artifact naming (plan 3.5), copied from the RunConfig the
  // runner executed. Carried on the result because that is what the report writer and
  // the history index have in hand -- neither of them sees the config.
  //
  // Not derivable from the ids above: a config file's name and the id inside it are
  // independent. Empty means "not resolved", which the naming code renders as "default"
  // rather than guessing a file.
  std::string trigger_profile_file;
  std::string threshold_config_file;

  // The web run id, when this result came from a web run. Empty for a CLI run, which has
  // no server to ask for a kernel log -- and the report's Export DMESG control stays
  // disabled in exactly that case.
  //
  // Carried so the report can put the ID (and only the ID) in the download URL: the
  // server resolves the record and generates the filename itself (plan 3.4).
  std::string run_id;

  std::vector<CameraRunResult> cameras;
};

struct RunConfig {
  // Only the device path. Per-camera `profile_id` and `trigger_channel_id` were
  // removed in v4: one run uses one Trigger Profile, and which channel a camera
  // fires on follows from its ROLE plus the profile's role_bindings. Carrying them
  // per camera meant the same routing was stated in several places and reconciled
  // at run time with an equality check.
  struct CameraConfig {
    std::string path;
  };

  // The single camera under test: the full test suite (t01..t26 minus
  // t25) runs against this camera only.
  CameraConfig master;
  // Extra cameras that exist solely to participate in t25-multi-camera as
  // additional watchers of the master's (or their own, if on a different
  // physical GPIO line) trigger pulse. Empty means t25 is skipped.
  std::vector<CameraConfig> slaves;
  // The single run-level Trigger Profile. Empty for free-run, which does not route
  // at all. Roles come from the topology above: master, then slave-1..slave-N in
  // slaves[] order (see role_bindings.hpp).
  std::string trigger_profile_id;
  TriggerMode trigger_mode = TriggerMode::FreeRun;
  std::vector<MemoryBackend> memory_backends;
  std::vector<std::string> test_selectors;
  // No report_formats: every run writes HTML, JSON and Markdown (plan 2.10). The
  // artifact list that comes back still carries a format per file.
  std::string output_directory = "reports";
  std::string config_directory;
  // Name of the threshold configuration to apply when deciding Pass/Warn/Fail.
  // "default" (or empty) uses the built-in defaults; other ids resolve to a
  // user config in the threshold directory. See ThresholdRegistry.
  std::string threshold_config_id = "default";
  // Source file names for artifact naming (plan 3.5.2), RESOLVED by the caller from the
  // registries -- never taken from the request.
  //
  // The ids above cannot stand in for these: a config file's name and the id inside it
  // are independent, so naming an artifact from an id would put the wrong name on it.
  // Empty means "not resolved", which the naming code renders as "default" rather than
  // guessing a file.
  std::string trigger_profile_file;
  std::string threshold_config_file;
  RunMode run_mode = RunMode::Sequential;

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
// Inverse of to_string(TestStatus). Needed to read a stored result back; an
// unrecognised value returns false rather than defaulting, because the status decides
// how the result is presented.
bool parse_test_status(const std::string &value, TestStatus *status);
bool parse_trigger_mode(const std::string &value, TriggerMode *mode);

std::vector<std::string> split_csv(const std::string &value);
std::string trim(const std::string &value);
std::string utc_timestamp();
std::string host_name();
std::string kernel_release();
std::string kernel_version();

}  // namespace v4l2diag
