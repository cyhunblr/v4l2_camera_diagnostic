#include "v4l2diag/core/report_writer.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unistd.h>

namespace {

std::string make_temp_dir() {
  std::string pattern = "/tmp/v4l2diag-report-test-XXXXXX";
  char *buffer = new char[pattern.size() + 1];
  std::copy(pattern.begin(), pattern.end(), buffer);
  buffer[pattern.size()] = '\0';
  char *created = mkdtemp(buffer);
  std::string result = created ? created : "/tmp/v4l2diag-report-test";
  delete[] buffer;
  return result;
}

bool exists(const std::string &path) {
  std::ifstream in(path);
  return static_cast<bool>(in);
}

std::string read_file(const std::string &path) {
  std::ifstream in(path);
  std::ostringstream contents;
  contents << in.rdbuf();
  return contents.str();
}

std::size_t occurrence_count(const std::string &value, const std::string &needle) {
  std::size_t count = 0;
  std::size_t position = 0;
  while ((position = value.find(needle, position)) != std::string::npos) {
    ++count;
    position += needle.size();
  }
  return count;
}

bool require(bool condition, const std::string &message) {
  if (!condition)
    std::cerr << message << "\n";
  return condition;
}

}  // namespace

int main() {
  v4l2diag::RunResult result;
  result.started_at_utc = "2026-01-01T00:00:00Z";
  result.finished_at_utc = "2026-01-01T00:00:01Z";
  result.host_name = "test-host";
  v4l2diag::CameraRunResult camera;
  camera.camera_path = "/dev/video-test";
  camera.profile_id = "test";
  camera.memory_backends.push_back(v4l2diag::MemoryBackend::Mmap);

  v4l2diag::TestResult latency_test;
  latency_test.id = "t14-trigger-latency";
  latency_test.name = "Trigger Latency";
  latency_test.category = "performance";
  latency_test.memory_backend = "mmap";
  latency_test.status = v4l2diag::TestStatus::Pass;
  latency_test.summary = "Latency is stable.";
  latency_test.duration_ms = 1050.0;
  latency_test.metrics = {
      {"latency_mean", "ms", 8.7, "Mean trigger latency."},
      {"latency_stddev", "ms", 0.1, "Std-dev trigger latency."},
      {"latency_min", "ms", 8.5, "Minimum trigger latency."},
      {"latency_p95", "ms", 15.8, "P95 trigger latency."},
      {"latency_max", "ms", 23.2, "Maximum trigger latency."},
      {"latency_jitter", "ms", 0.2, "Jitter trigger latency."},
  };
  camera.tests.push_back(latency_test);

  v4l2diag::TestResult sweep_test;
  sweep_test.id = "t16-gpio-pulse-width";
  sweep_test.name = "GPIO Pulse Width";
  sweep_test.category = "trigger";
  sweep_test.memory_backend = "mmap";
  sweep_test.status = v4l2diag::TestStatus::Warn;
  sweep_test.summary = "Pulse sweep completed with warnings.";
  sweep_test.duration_ms = 2200.0;
  sweep_test.metrics = {
      {"hits_1ms", "count", 2.0, "Hits at 1ms."},    {"hits_2ms", "count", 5.0, "Hits at 2ms."},
      {"hits_3ms", "count", 8.0, "Hits at 3ms."},    {"hits_5ms", "count", 12.0, "Hits at 5ms."},
      {"hits_7ms", "count", 16.0, "Hits at 7ms."},   {"hits_10ms", "count", 20.0, "Hits at 10ms."},
      {"hits_13ms", "count", 20.0, "Hits at 13ms."},
  };
  camera.tests.push_back(sweep_test);

  v4l2diag::TestResult overwrite_test;
  overwrite_test.id = "t08-buffer-overwrite";
  overwrite_test.name = "Buffer Overwrite";
  overwrite_test.category = "stress";
  overwrite_test.memory_backend = "mmap";
  overwrite_test.status = v4l2diag::TestStatus::Warn;
  overwrite_test.summary = "Buffer saturation test completed; 1 frames had V4L2_BUF_FLAG_ERROR.";
  overwrite_test.duration_ms = 1300.0;
  overwrite_test.metrics = {
      {"triggers_A", "count", 10.0, "Variant A."},
      {"frames_available_A", "count", 2.0, "Frames available after variant A."},
      {"error_flag_total", "count", 1.0, "Frames with V4L2_BUF_FLAG_ERROR."},
  };
  overwrite_test.details = {
      "Variant A: buffers=2 triggers=10 available=2 errors=1",
      "Error flag buffers: variant A buffer_index=1 sequence=42 flags=0x4000",
  };
  camera.tests.push_back(overwrite_test);

  v4l2diag::TestResult inventory_test;
  inventory_test.id = "t01-device-compliance";
  inventory_test.name = "Device Compliance";
  inventory_test.category = "compliance";
  inventory_test.status = v4l2diag::TestStatus::Skipped;
  inventory_test.summary = "Inventory values recorded.";
  inventory_test.duration_ms = 12.0;
  inventory_test.metrics = {
      {"supports_capture", "bool", 1.0, "Capture capability."},
      {"format_count", "count", 12.0, "Formats enumerated."},
      {"cliff_ms", "ms", -1.0, "No cliff found."},
      {"safety_margin_ms", "ms", -500.0, "N/A."},
  };
  camera.tests.push_back(inventory_test);

  v4l2diag::TestResult cliff_test;
  cliff_test.id = "t13-poll-timeout-cliff";
  cliff_test.name = "Poll Timeout Cliff";
  cliff_test.category = "trigger";
  cliff_test.memory_backend = "mmap";
  cliff_test.status = v4l2diag::TestStatus::Pass;
  cliff_test.summary = "Stable cliff at 45ms; 55ms safety margin.";
  cliff_test.duration_ms = 61000.0;
  cliff_test.metrics = {
      {"cliff_ms", "ms", 45.0, "Stable poll timeout."},
      {"cliff_total_ms", "ms", 50.0, "Cliff plus pulse width."},
      {"first_miss_ms", "ms", 44.0, "Highest timeout with misses."},
      {"safety_margin_ms", "ms", 55.0, "Production timeout - cliff timeout."},
      {"stability_confirmed", "bool", 1.0, "Whether cliff was stable."},
      {"stability_rounds_passed", "count", 3.0, "Rounds that confirmed the cliff."},
  };
  cliff_test.details = {
      "coarse: 100ms -> 4/4", "coarse: 50ms -> 4/4",  "coarse: 40ms -> 2/4",
      "bsearch: 45ms -> 4/4", "bsearch: 44ms -> 3/4", "stability round 1: @45ms=4/4, @44ms=3/4 OK",
  };
  camera.tests.push_back(cliff_test);

  v4l2diag::TestResult delta_test;
  delta_test.id = "t24-latency-under-load";
  delta_test.name = "Latency Under Load";
  delta_test.category = "performance";
  delta_test.status = v4l2diag::TestStatus::Pass;
  delta_test.summary = "Load delta is acceptable.";
  delta_test.duration_ms = 4100.0;
  delta_test.metrics = {
      {"delta_mean_ms", "ms", -1.0, "Mean latency change under load."},
      {"delta_p95_ms", "ms", 1.25, "P95 latency change under load."},
  };
  camera.tests.push_back(delta_test);
  result.cameras.push_back(camera);

  const std::string dir = make_temp_dir();
  const auto artifacts = v4l2diag::write_reports(result,
                                                 {v4l2diag::ReportFormat::Json, v4l2diag::ReportFormat::Markdown,
                                                  v4l2diag::ReportFormat::Html, v4l2diag::ReportFormat::Pdf},
                                                 dir);

  if (!require(artifacts.size() == 4, "unexpected report artifact count") ||
      !require(exists(dir + "/diagnostic-report.json"), "missing JSON report") ||
      !require(exists(dir + "/diagnostic-report.md"), "missing Markdown report") ||
      !require(exists(dir + "/diagnostic-report.html"), "missing HTML report") ||
      !require(exists(dir + "/diagnostic-report.pdf"), "missing PDF report")) {
    return 1;
  }

  const std::string html = read_file(dir + "/diagnostic-report.html");
  bool html_ok = true;
  html_ok &= require(html.find("Result Distribution") != std::string::npos, "missing result distribution");
  html_ok &= require(html.find("summary-badge") == std::string::npos, "legacy summary badges remain");
  html_ok &= require(html.find("metric-card") == std::string::npos, "legacy metric cards remain");
  html_ok &= require(html.find("Latency Snapshot") == std::string::npos, "unexpected latency snapshot");
  html_ok &= require(html.find("Test Duration") == std::string::npos, "unexpected test duration chart");
  html_ok &= require(html.find("class=\"metric-point\"") != std::string::npos, "missing X-Y point markers");
  html_ok &= require(html.find("class=\"guide-line\"") != std::string::npos, "missing X-Y guide lines");
  html_ok &= require(html.find("stroke-dasharray") != std::string::npos, "guide lines are not dashed");
  html_ok &= require(html.find("cx=\"60\"") == std::string::npos, "X-Y point marker is stuck on the left axis");
  html_ok &= require(html.find("cx=\"736\"") == std::string::npos, "X-Y point marker is stuck on the right axis");
  html_ok &= require(html.find("cy=\"38\"") == std::string::npos, "X-Y point marker is stuck on the top axis");
  html_ok &= require(html.find("cy=\"202\"") == std::string::npos, "X-Y point marker is stuck on the bottom axis");
  html_ok &= require(html.find("metric-bars") != std::string::npos, "missing horizontal fallback chart");
  html_ok &= require(html.find("metric-kv-list") != std::string::npos, "missing plain metric list");
  html_ok &= require(html.find("Supporting values") != std::string::npos, "missing supporting values label");
  html_ok &= require(html.find("Supports capture") != std::string::npos, "bool metric is not in the plain list");
  html_ok &= require(html.find("Format count") != std::string::npos, "count metric is not in the plain list");
  html_ok &=
      require(occurrence_count(html, "data-metric=\"latency_mean\"") == 1, "charted latency metric is duplicated");
  html_ok &= require(occurrence_count(html, "data-metric=\"latency_stddev\"") == 0,
                     "latency stddev should not compress the primary latency chart");
  html_ok &=
      require(html.find("Latency stddev") != std::string::npos, "latency stddev is missing from supporting values");
  html_ok &= require(occurrence_count(html, "data-metric=\"hits_1ms\"") == 1, "charted sweep metric is duplicated");
  html_ok &= require(html.find("Error flag buffers") != std::string::npos, "t08 error buffer details are missing");
  html_ok &= require(html.find("buffer_index=1") != std::string::npos, "t08 error buffer index is missing");
  html_ok &= require(html.find("t13-distribution-chart") != std::string::npos, "missing t13 distribution chart");
  html_ok &= require(html.find("data-timeout-ms=\"45\"") != std::string::npos, "missing t13 timeout point");
  html_ok &= require(html.find("t13-threshold-chart") != std::string::npos, "missing t13 threshold chart");
  html_ok &= require(html.find("data-metric=\"production_timeout_ms\"") != std::string::npos,
                     "missing inferred production timeout marker");
  html_ok &= require(occurrence_count(html, "data-metric=\"cliff_ms\"") == 1, "t13 cliff metric is duplicated");
  html_ok &= require(occurrence_count(html, "<dt>Cliff ms</dt>") == 1, "t13 cliff key/value duplicate remains");
  html_ok &= require(html.find("data-metric=\"delta_mean_ms\"") != std::string::npos,
                     "negative non-sentinel delta was not charted");
  html_ok &= require(occurrence_count(html, ">N/A</dd>") == 2, "sentinel metrics were not rendered as N/A");
  html_ok &= require(html.find("Failed</span><strong>0</strong>") != std::string::npos,
                     "zero-count status is missing from the legend");
  html_ok &= require(html.find("fetch(\"/api/dmesg\")") == std::string::npos, "dmesg export still uses fetch");
  html_ok &= require(html.find("href=\"/api/dmesg?download=1\" download=\"dmesg.txt\"") != std::string::npos,
                     "dmesg export is not a direct download link");
  if (!html_ok)
    return 1;

  const std::string json = read_file(dir + "/diagnostic-report.json");
  const std::string markdown = read_file(dir + "/diagnostic-report.md");
  if (!require(json.find("\"name\":\"latency_mean\"") != std::string::npos, "JSON metrics changed unexpectedly") ||
      !require(markdown.find("| latency_mean |") != std::string::npos, "Markdown metrics changed unexpectedly")) {
    return 1;
  }

  std::cout << dir << "\n";
  return 0;
}
