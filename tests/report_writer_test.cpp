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
  // The full pws[] sweep the test actually performs: 11 categories is what made
  // the old "tone-{index % 4}" palette give 3ms and 30ms the same fill.
  sweep_test.metrics = {
      {"hits_1ms", "count", 2.0, "Hits at 1ms."},    {"hits_2ms", "count", 5.0, "Hits at 2ms."},
      {"hits_3ms", "count", 8.0, "Hits at 3ms."},    {"hits_5ms", "count", 12.0, "Hits at 5ms."},
      {"hits_7ms", "count", 16.0, "Hits at 7ms."},   {"hits_10ms", "count", 20.0, "Hits at 10ms."},
      {"hits_13ms", "count", 20.0, "Hits at 13ms."}, {"hits_15ms", "count", 20.0, "Hits at 15ms."},
      {"hits_20ms", "count", 20.0, "Hits at 20ms."}, {"hits_25ms", "count", 20.0, "Hits at 25ms."},
      {"hits_30ms", "count", 20.0, "Hits at 30ms."},
  };
  camera.tests.push_back(sweep_test);

  v4l2diag::TestResult format_test;
  format_test.id = "t17-format-comparison";
  format_test.name = "Format Comparison";
  format_test.category = "format";
  format_test.memory_backend = "mmap";
  format_test.status = v4l2diag::TestStatus::Pass;
  format_test.summary = "Two unique formats compared.";
  format_test.duration_ms = 3200.0;
  format_test.metrics = {
      {"uyvy_latency_mean", "ms", 468.6, "UYVY mean latency."},
      {"uyvy_latency_max", "ms", 486.5, "UYVY max latency."},
      {"uyvy_throughput_mbps", "MB/s", 1064.8, "UYVY throughput."},
      {"nv16_latency_mean", "ms", 477.6, "NV16 mean latency."},
      {"nv16_latency_max", "ms", 481.4, "NV16 max latency."},
      {"nv16_throughput_mbps", "MB/s", 1056.9, "NV16 throughput."},
      {"format_count", "count", 3.0, "Unique formats enumerated."},
      {"formats_tested", "count", 2.0, "Unique formats tested."},
  };
  // YUYV is enumerated but never measured, so it produces no metrics at all and
  // only shows up in the details. It has to reach the report as an explicit
  // omission instead of silently disappearing from the chart.
  format_test.details = {
      "UYVY: sizeimage=4915200",
      "NV16: sizeimage=4915200",
      "YUYV: S_FMT failed",
  };
  camera.tests.push_back(format_test);

  v4l2diag::TestResult resolution_test;
  resolution_test.id = "t19-resolution-sweep";
  resolution_test.name = "Resolution Sweep";
  resolution_test.category = "format";
  resolution_test.memory_backend = "mmap";
  resolution_test.status = v4l2diag::TestStatus::Pass;
  resolution_test.summary = "Four resolutions compared.";
  resolution_test.duration_ms = 4100.0;
  // Every resolution has to land on one chart per unit; the old grouping fell
  // back to a separate dot chart per resolution, which made them incomparable.
  resolution_test.metrics = {
      {"640x480_latency_mean", "ms", 12.4, "Mean latency at 640x480."},
      {"640x480_latency_p95", "ms", 15.1, "P95 latency at 640x480."},
      {"640x480_throughput_mbps", "MB/s", 1890.0, "Throughput at 640x480."},
      {"1280x720_latency_mean", "ms", 18.9, "Mean latency at 1280x720."},
      {"1280x720_latency_p95", "ms", 23.7, "P95 latency at 1280x720."},
      {"1280x720_throughput_mbps", "MB/s", 1540.0, "Throughput at 1280x720."},
      {"1920x1080_latency_mean", "ms", 27.3, "Mean latency at 1920x1080."},
      {"1920x1080_latency_p95", "ms", 34.8, "P95 latency at 1920x1080."},
      {"1920x1080_throughput_mbps", "MB/s", 1210.0, "Throughput at 1920x1080."},
      {"2896x1876_latency_mean", "ms", 41.6, "Mean latency at 2896x1876."},
      {"2896x1876_latency_p95", "ms", 52.2, "P95 latency at 2896x1876."},
      {"2896x1876_throughput_mbps", "MB/s", 1056.0, "Throughput at 2896x1876."},
      {"resolution_count", "count", 5.0, "Resolutions tested."},
  };
  resolution_test.details = {
      "640x480: mean=12ms p95=15ms throughput=1890MB/s",
      "1280x720: mean=18ms p95=23ms throughput=1540MB/s",
      "1920x1080: mean=27ms p95=34ms throughput=1210MB/s",
      "2896x1876: mean=41ms p95=52ms throughput=1056MB/s",
      "3840x2160: S_FMT failed — skipped",
  };
  camera.tests.push_back(resolution_test);

  v4l2diag::TestResult control_test;
  control_test.id = "t18-control-sweep";
  control_test.name = "Control Sweep";
  control_test.category = "control";
  control_test.memory_backend = "mmap";
  control_test.status = v4l2diag::TestStatus::Pass;
  control_test.summary = "Control combinations measured.";
  control_test.duration_ms = 2800.0;
  // All 8 combinations of the ISX021 sweep, as the test emits them.
  control_test.metrics = {
      {"ll0_bp0_wi0_mean_ms", "ms", 4.2, "Control combination latency."},
      {"ll0_bp0_wi1_mean_ms", "ms", 4.9, "Control combination latency."},
      {"ll0_bp1_wi0_mean_ms", "ms", 3.8, "Control combination latency."},
      {"ll0_bp1_wi1_mean_ms", "ms", 4.4, "Control combination latency."},
      {"ll1_bp0_wi0_mean_ms", "ms", 5.6, "Control combination latency."},
      {"ll1_bp0_wi1_mean_ms", "ms", 5.1, "Control combination latency."},
      {"ll1_bp1_wi0_mean_ms", "ms", 6.0, "Control combination latency."},
      {"ll1_bp1_wi1_mean_ms", "ms", 6.4, "Control combination latency."},
  };
  camera.tests.push_back(control_test);

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
      {"cliff_ms", "ms", 45.0, "Stable poll timeout, measured from the trigger's rising edge."},
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
  const auto passed_pos = html.find("<span>Passed</span>");
  const auto warnings_pos = html.find("<span>Warnings</span>");
  const auto failed_pos = html.find("<span>Failed</span>");
  const auto skipped_pos = html.find("<span>Skipped</span>");
  html_ok &= require(passed_pos != std::string::npos && warnings_pos != std::string::npos &&
                         failed_pos != std::string::npos && skipped_pos != std::string::npos &&
                         passed_pos < warnings_pos && warnings_pos < failed_pos && failed_pos < skipped_pos,
                     "result distribution order is not Passed-Warnings-Failed-Skipped");
  html_ok &= require(html.find("summary-badge") == std::string::npos, "legacy summary badges remain");
  html_ok &= require(html.find("metric-card") == std::string::npos, "legacy metric cards remain");
  html_ok &= require(html.find("Latency Snapshot") == std::string::npos, "unexpected latency snapshot");
  html_ok &= require(html.find("Test Duration") == std::string::npos, "unexpected test duration chart");
  html_ok &= require(html.find("overflow-x: auto") == std::string::npos, "chart horizontal scrollbar CSS remains");
  html_ok &= require(html.find("min-width: 620px") == std::string::npos, "chart minimum width still forces scrolling");
  html_ok &= require(html.find("metric-dot-chart") != std::string::npos, "missing statistic dot-range chart");
  html_ok &= require(html.find("metric-vertical-bars") != std::string::npos, "missing vertical sweep chart");
  html_ok &= require(html.find("class=\"metric-point\"") != std::string::npos, "missing statistic point markers");
  html_ok &= require(html.find("class=\"guide-line\"") != std::string::npos, "missing statistic guide lines");
  html_ok &= require(html.find("stroke-dasharray") != std::string::npos, "guide lines are not dashed");

  // Axis roles: the category axis names the independent variable and the value
  // axis names the measured quantity. Neither is derived from the chart title,
  // and neither carries the old "X: " / "Y: " prefix.
  html_ok &= require(html.find(">X: ") == std::string::npos, "axis captions still carry the X: prefix");
  html_ok &= require(html.find(">Y: ") == std::string::npos, "axis captions still carry the Y: prefix");
  html_ok &= require(html.find(">Pixel format</text>") != std::string::npos, "format category axis is not labelled");
  html_ok &= require(html.find(">Capture latency (ms)</text>") != std::string::npos,
                     "format latency value axis is not labelled with the measured quantity");
  html_ok &= require(html.find(">Memcpy throughput (MB/s)</text>") != std::string::npos,
                     "throughput value axis is not labelled with the measured quantity");
  html_ok &= require(html.find("Latency Sweep (ms)") == std::string::npos,
                     "value axis is still labelled with the chart title");
  html_ok &= require(html.find(">Pulse width (ms)</text>") != std::string::npos,
                     "t16 category axis is not labelled with the swept quantity");
  html_ok &= require(html.find(">Trigger hits (count)</text>") != std::string::npos,
                     "t16 value axis is not labelled with the measured quantity");

  html_ok &= require(html.find("metric-bars") != std::string::npos, "missing horizontal bar chart");
  html_ok &= require(html.find("class=\"horizontal-bar\"") != std::string::npos,
                     "horizontal bars are not rendered as SVG marks");
  html_ok &= require(html.find("bar-fill") == std::string::npos, "legacy CSS horizontal bar fills remain");
  html_ok &= require(html.find("horizontal-y-axis") == std::string::npos, "legacy CSS horizontal axis remains");
  html_ok &= require(html.find("horizontal-bar-plot") == std::string::npos, "legacy CSS horizontal plot remains");

  // t18: the control combination labels spell the controls out. The metric keys
  // stay untouched -- docs and the threshold registry reference them.
  html_ok &= require(html.find("Ll0 bp0 wi0 mean ms") == std::string::npos, "t18 still shows raw metric-name labels");
  html_ok &= require(html.find("LED 0 \xc2\xb7 BYP 1 \xc2\xb7 WIN 0") != std::string::npos,
                     "t18 control combination label is missing");
  html_ok &= require(html.find("data-metric=\"ll0_bp1_wi0_mean_ms\"") != std::string::npos,
                     "t18 metric keys should not change");
  html_ok &= require(html.find(">Mean capture latency (ms)</text>") != std::string::npos,
                     "t18 value axis is not labelled with the measured quantity");
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
  html_ok &=
      require(occurrence_count(html, "data-metric=\"uyvy_latency_mean\"") == 1, "charted format metric is duplicated");
  html_ok &=
      require(occurrence_count(html, ">UYVY</text>") == 2, "UYVY should appear once on each format comparison chart");
  html_ok &=
      require(occurrence_count(html, ">NV16</text>") == 2, "NV16 should appear once on each format comparison chart");
  html_ok &= require(html.find(">Mean</span>") != std::string::npos, "format latency mean legend is missing");
  html_ok &= require(html.find(">Max</span>") != std::string::npos, "format latency max legend is missing");

  // YUYV was enumerated but never measured: it must be reported, not dropped.
  html_ok &= require(html.find("chart-omissions") != std::string::npos, "unmeasured formats are not reported");
  html_ok &= require(html.find("YUYV") != std::string::npos, "unmeasured YUYV is missing from the report");
  html_ok &= require(html.find("S_FMT failed") != std::string::npos, "unmeasured format reason is missing");
  html_ok &=
      require(occurrence_count(html, ">YUYV</text>") == 0, "unmeasured YUYV should not be charted as a category");
  html_ok &= require(html.find("3840x2160") != std::string::npos, "unmeasured resolution is missing from the report");

  // t19: every resolution belongs on one chart per unit, not one chart each.
  html_ok &= require(html.find(">Resolution</text>") != std::string::npos, "t19 category axis is not labelled");
  html_ok &= require(html.find("640x480 Latency") == std::string::npos, "t19 still emits a chart per resolution");
  html_ok &= require(occurrence_count(html, ">640x480</text>") == 2,
                     "each resolution should appear once per t19 chart (latency and throughput)");
  html_ok &= require(occurrence_count(html, ">2896x1876</text>") == 2,
                     "the largest resolution should appear once per t19 chart");
  html_ok &=
      require(html.find("data-metric=\"1920x1080_latency_p95\"") != std::string::npos, "t19 p95 series is not charted");

  // Colour never cycles: no tone-* classes survive, and the sequential ramp is
  // keyed to the category so distant categories cannot share a fill.
  html_ok &= require(html.find("tone-0") == std::string::npos && html.find("tone-1") == std::string::npos &&
                         html.find("tone-2") == std::string::npos && html.find("tone-3") == std::string::npos,
                     "cycling tone-* palette classes remain");
  html_ok &=
      require(html.find("fill=\"#d97706\"") == std::string::npos, "the warning colour is still used for a data series");
  html_ok &=
      require(html.find("fill=\"#e2553d\"") == std::string::npos, "the failure colour is still used for a data series");
  html_ok &= require(html.find("#e2553d") == std::string::npos, "the old series palette is still defined");
  html_ok &= require(html.find("chart-ramp-key") != std::string::npos, "sequential ramp has no scale key");
  html_ok &= require(html.find("fill=\"#86b6ef\"") != std::string::npos, "sequential ramp light end is missing");
  html_ok &= require(html.find("fill=\"#0d366b\"") != std::string::npos, "sequential ramp dark end is missing");

  // Ticks are round numbers rather than max*1.18 fractions.
  html_ok &= require(html.find(">23.6</text>") == std::string::npos, "axis ticks are still scaled by max*1.18");
  html_ok &=
      require(html.find("metric-chart--wide") != std::string::npos, "many-category charts do not take the full row");
  html_ok &= require(html.find("Error flag buffers") != std::string::npos, "t08 error buffer details are missing");
  html_ok &= require(html.find("buffer_index=1") != std::string::npos, "t08 error buffer index is missing");
  html_ok &= require(html.find("t13-distribution-chart") != std::string::npos, "missing t13 distribution chart");
  html_ok &= require(html.find("distribution-area") != std::string::npos, "t13 distribution area is missing");
  html_ok &= require(html.find("hit-zone-high") != std::string::npos, "t13 distribution bands are missing");
  html_ok &= require(html.find("data-timeout-ms=\"45\"") != std::string::npos, "missing t13 timeout point");
  html_ok &= require(html.find("t13-threshold-chart") != std::string::npos, "missing t13 threshold chart");
  html_ok &= require(html.find("threshold-risk") != std::string::npos, "t13 risk threshold band is missing");
  html_ok &= require(html.find("threshold-margin") != std::string::npos, "t13 margin threshold band is missing");
  html_ok &= require(html.find("threshold-safe") != std::string::npos, "t13 safe threshold band is missing");
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
