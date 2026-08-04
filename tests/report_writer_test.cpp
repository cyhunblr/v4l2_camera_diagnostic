#include "v4l2diag/core/report_naming.hpp"
#include "v4l2diag/core/report_writer.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include "v4l2diag/core/run_result_json.hpp"

#include <json/json.h>

#include <sstream>
#include <sys/stat.h>
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

// The canonical artifact name a result gets (plan 3.5). The fixed "diagnostic-report.*"
// is gone: the name now carries the run's start time, trigger mode and source config
// files, so a folder of archived runs no longer has three files all called the same thing.
std::string artifact_name(const v4l2diag::RunResult &run, v4l2diag::ReportFormat format) {
  v4l2diag::ReportNaming naming;
  naming.started_at_utc = run.started_at_utc;
  naming.trigger_mode = run.trigger_mode;
  naming.trigger_profile_file = run.trigger_profile_file;
  naming.test_configuration_file = run.threshold_config_file;
  return v4l2diag::report_artifact_filename(naming, format);
}

int main() {
  v4l2diag::RunResult result;
  result.started_at_utc = "2026-01-01T00:00:00Z";
  result.finished_at_utc = "2026-01-01T00:00:01Z";
  result.host_name = "test-host";
  // Run-level trigger contract (plan 2.5, report model (a)): stated once here, not
  // repeated per camera.
  result.trigger_mode = v4l2diag::TriggerMode::Hardware;
  result.trigger_profile_id = "anvil";
  // A triggered run must carry the profile's SOURCE FILE, not just its id: artifacts are
  // named from the file (plan 3.5.2) and a triggered run has no default. The id and the
  // filename genuinely differ -- see the "anvil" / "anvil-v2.json" pairing here.
  result.trigger_profile_file = "anvil-v2.json";
  result.threshold_config_file = "stress-test.json";
  // Timing is a run-level field now, filled once by the runner.
  result.trigger_rate_hz = 12.5;
  result.pulse_width_ms = 3.25;
  {
    v4l2diag::RoleBinding master_binding;
    master_binding.role = "master";
    master_binding.trigger_channel_id = "trigger-channel-a";
    result.role_bindings.push_back(master_binding);
    v4l2diag::RoleBinding slave_binding;
    slave_binding.role = "slave-1";
    slave_binding.trigger_channel_id = "trigger-channel-b";
    result.role_bindings.push_back(slave_binding);
  }

  v4l2diag::CameraRunResult camera;
  camera.camera_path = "/dev/video-test";
  camera.role = "master";
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

  // A device offering a single format charts nothing — one bar is not a
  // comparison — so there is no measured set to contrast an omission against.
  // The omission heuristic reads "<label>: <value>" detail lines, and with an
  // empty chart it used to flag this test's own measured format as "Not
  // measured". Notes carry the explanation instead.
  v4l2diag::TestResult single_format_test;
  single_format_test.id = "t17-format-comparison";
  single_format_test.name = "Format Comparison";
  single_format_test.category = "format";
  single_format_test.memory_backend = "userptr";
  single_format_test.status = v4l2diag::TestStatus::Warn;
  single_format_test.summary = "Measured 1 format, but a comparison needs at least two.";
  single_format_test.duration_ms = 1100.0;
  single_format_test.metrics = {
      {"yuyv_latency_mean", "ms", 83.4, "YUYV mean latency."},
      {"yuyv_throughput_mbps", "MB/s", 10216.0, "YUYV throughput."},
      {"format_count", "count", 1.0, "Unique formats enumerated."},
      {"formats_tested", "count", 1.0, "Unique formats tested."},
  };
  single_format_test.details = {"YUYV: sizeimage=4915200"};
  single_format_test.notes = {"This device offers only one pixel format, so there was nothing to compare it with."};
  camera.tests.push_back(single_format_test);

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
  // review-plan 5.8: the structured variant/evidence detail format T08's own content
  // renderer reads.
  overwrite_test.details = {
      "variant: Variant A|10 at 10/s|2|2|2|1|2|WARN",
      "evidence: Variant A|Buffer 1, sequence 42: MAPPED|0x4000",
      "settle_time: 500ms",
      "backend_memory: mmap",
      "variant_a_config: 10 / 100ms",
      "variant_b_config: n/a",
      "error_threshold: 0",
  };
  overwrite_test.metrics.push_back({"allocated_buffers", "count", 2.0, "Allocated buffers."});
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
  // The single public API: every run writes all three (plan 2.10). There is no
  // subset overload to pick from.
  const auto artifacts = v4l2diag::write_reports(result, dir);

  if (!require(artifacts.size() == 3, "unexpected report artifact count") ||
      !require(exists(dir + "/" + artifact_name(result, v4l2diag::ReportFormat::Json)), "missing JSON report") ||
      !require(exists(dir + "/" + artifact_name(result, v4l2diag::ReportFormat::Markdown)),
               "missing Markdown report") ||
      !require(exists(dir + "/" + artifact_name(result, v4l2diag::ReportFormat::Html)), "missing HTML report") ||
      // The fixed pre-3.5 name must be gone, not merely accompanied.
      !require(!exists(dir + "/diagnostic-report.html"), "the fixed pre-3.5 name is still written") ||
      // The product writes no PDF: the HTML report's Export button calls
      // window.print() instead (report-ui-review-plan.md §6.2).
      !require(!exists(dir + "/diagnostic-report.pdf"), "a PDF report was written")) {
    return 1;
  }

  const std::string html = read_file(dir + "/" + artifact_name(result, v4l2diag::ReportFormat::Html));
  bool html_ok = true;
  html_ok &= require(html.find("Result Distribution") != std::string::npos, "missing result distribution");
  // "Warned", not "Warnings" (plan 3.6): these are counts of tests, and "Warnings" named
  // a list. The internal key, the data model and the JSON are unchanged.
  const auto passed_pos = html.find("<span>Passed</span>");
  const auto warned_pos = html.find("<span>Warned</span>");
  const auto failed_pos = html.find("<span>Failed</span>");
  const auto skipped_pos = html.find("<span>Skipped</span>");
  html_ok &= require(passed_pos != std::string::npos && warned_pos != std::string::npos &&
                         failed_pos != std::string::npos && skipped_pos != std::string::npos &&
                         passed_pos < warned_pos && warned_pos < failed_pos && failed_pos < skipped_pos,
                     "result distribution order is not Passed-Warned-Failed-Skipped");
  html_ok &=
      require(html.find("<span>Warnings</span>") == std::string::npos, "the old \"Warnings\" count label survived");
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
  // The axis-role contract is checked on t16, whose chart the approved preview DOES carry.
  //
  // It used to be checked on t17 and t18 as well -- but neither of their approved previews
  // has a chart at all, and plan 3.1 (review round 3) removed the unapproved ones. The
  // contract survives; only the test that demonstrates it moved.
  html_ok &= require(html.find("Latency Sweep (ms)") == std::string::npos,
                     "value axis is still labelled with the chart title");
  html_ok &= require(html.find(">Pulse width (ms)</text>") != std::string::npos,
                     "t16 category axis is not labelled with the swept quantity");
  html_ok &= require(html.find(">Trigger hits (count)</text>") != std::string::npos,
                     "t16 value axis is not labelled with the measured quantity");
  // The edge-latency chart is not asserted here: this fixture's t16 records only hits_*,
  // so no lat_high_avg_* series exists to chart. It is covered by the conformance render,
  // which supplies the full sweep (docs/assets/source-render/).

  // No chart for a test whose preview has none. Named individually so a regression says
  // which one came back.
  html_ok &= require(html.find(">Pixel format</text>") == std::string::npos,
                     "t17 renders a chart its approved preview does not have");
  html_ok &= require(html.find("metric-bars") == std::string::npos,
                     "t18 renders a horizontal bar chart its approved preview does not have");
  html_ok &= require(html.find(">Mean capture latency (ms)</text>") == std::string::npos,
                     "t18's unapproved chart axis survived");
  // The legacy CSS class names must stay gone regardless. Matched as a standalone class
  // name (with a boundary before it), since T06's approved ".reliability-bar-fill"
  // (review-plan 5.6.5) legitimately contains the substring "bar-fill".
  html_ok &= require(html.find(" bar-fill") == std::string::npos && html.find("\"bar-fill") == std::string::npos,
                     "legacy CSS horizontal bar fills remain");
  html_ok &= require(html.find("horizontal-y-axis") == std::string::npos, "legacy CSS horizontal axis remains");
  html_ok &= require(html.find("horizontal-bar-plot") == std::string::npos, "legacy CSS horizontal plot remains");

  // t18: the control combination labels spell the controls out, in its TABLE now that the
  // chart is gone. The metric keys stay untouched -- docs and the threshold registry
  // reference them.
  html_ok &= require(html.find("Ll0 bp0 wi0 mean ms") == std::string::npos, "t18 still shows raw metric-name labels");
  html_ok &= require(html.find("LED 0 \xc2\xb7 BYP 1 \xc2\xb7 WIN 0") != std::string::npos,
                     "t18 control combination label is missing");
  // The generic key/value list and its "Supporting values" strip are GONE (plan 3.1,
  // review round 2): each test now presents its metrics in its own approved table, so the
  // strip would print the same numbers a second time under different labels.
  html_ok &=
      require(html.find("<dl class=\"metric-kv-list\">") == std::string::npos, "the generic metric-kv-list survived");
  html_ok &= require(html.find("supporting-title\">Supporting values<") == std::string::npos,
                     "the Supporting values strip survived");
  // What replaced it: a per-test table. This fixture's tests are registered, so each must
  // have one.
  html_ok &=
      require(html.find("<table class=\"evidence\">") != std::string::npos, "no per-test evidence table was rendered");
  // review-plan 5.1.5/5.1.6 renamed T01's visible presentation: the bool capability is a
  // labelled row with SUPPORTED beside it, and the format total is the section count.
  html_ok &=
      require(html.find("Capture support") != std::string::npos, "the capture capability is not presented at all");
  html_ok &= require(html.find("SUPPORTED") != std::string::npos, "no capability state is shown");
  html_ok &= require(html.find("12 formats") != std::string::npos, "the format count is not presented");
  // A charted metric appears once in its chart. Checked on t16, whose chart is approved;
  // the equivalent t17 assertions went with t17's unapproved chart.
  html_ok &= require(occurrence_count(html, "data-metric=\"hits_1ms\"") == 1, "charted sweep metric is duplicated");
  html_ok &= require(occurrence_count(html, "data-metric=\"latency_stddev\"") == 0,
                     "latency stddev should not compress the primary latency chart");
  html_ok &=
      require(html.find("Latency stddev") != std::string::npos, "latency stddev is missing from the test's table");
  // t17's formats are still named -- in its table, which is what the approved preview has.
  html_ok &= require(html.find("UYVY") != std::string::npos, "UYVY is missing from the format comparison table");
  html_ok &= require(html.find("NV16") != std::string::npos, "NV16 is missing from the format comparison table");

  // YUYV was enumerated but never measured: it must be reported, not dropped.
  html_ok &= require(html.find("chart-omissions") != std::string::npos, "unmeasured formats are not reported");
  html_ok &= require(html.find("YUYV") != std::string::npos, "unmeasured YUYV is missing from the report");
  html_ok &= require(html.find("S_FMT failed") != std::string::npos, "unmeasured format reason is missing");
  html_ok &=
      require(occurrence_count(html, ">YUYV</text>") == 0, "unmeasured YUYV should not be charted as a category");
  html_ok &= require(html.find("3840x2160") != std::string::npos, "unmeasured resolution is missing from the report");

  // t19: every resolution belongs on one chart per unit, not one chart each.
  // t19's chart went with the other unapproved ones (plan 3.1, review round 3): its
  // approved preview has no <svg> at all. The resolutions are still named -- in t19's own
  // "Resolution | Coverage | Mean | P95 | State" table, which IS the approved presentation.
  html_ok &= require(html.find(">Resolution</text>") == std::string::npos,
                     "t19 renders a chart its approved preview does not have");
  html_ok &= require(html.find("640x480 Latency") == std::string::npos, "t19 still emits a chart per resolution");
  html_ok &= require(html.find("640x480") != std::string::npos, "a measured resolution is missing from the report");
  html_ok &= require(html.find("2896x1876") != std::string::npos, "a measured resolution is missing from the report");

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
  // review-plan 5.8.8: the decoded flag evidence, with the raw hex value kept alongside it.
  html_ok &= require(html.find("Buffer 1, sequence 42") != std::string::npos, "t08 error buffer evidence is missing");
  html_ok &= require(html.find("0x4000") != std::string::npos, "t08's raw flag value is missing");
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
  // A recorded metric now appears in its own test's section, once. T01's fixture records
  // cliff_ms, so it shows under "Recorded values" with its sentinel rendered as N/A.
  html_ok &= require(occurrence_count(html, "<dt>Cliff ms</dt>") == 1,
                     "the recorded cliff metric is not presented exactly once");
  html_ok &= require(html.find("data-metric=\"delta_mean_ms\"") != std::string::npos,
                     "negative non-sentinel delta was not charted");
  // Sentinel metrics still read "N/A" -- the contract survived the move from the <dl> to
  // the per-test tables. A sentinel rendered as a number would put "-500 ms" in a report.
  html_ok &= require(html.find(">N/A<") != std::string::npos, "sentinel metrics were not rendered as N/A");
  html_ok &= require(html.find("-500") == std::string::npos, "a sentinel metric was rendered as a raw number");
  html_ok &= require(html.find("Failed</span><strong>0</strong>") != std::string::npos,
                     "zero-count status is missing from the legend");
  html_ok &= require(html.find("fetch(\"/api/dmesg\")") == std::string::npos, "dmesg export still uses fetch");
  // Plan 3.4: the fixed "dmesg.txt" is gone and the client no longer names the file at
  // all. The control is a real disabled button that only enables when the page is being
  // served, and the download URL carries the run id -- the SERVER generates the filename
  // from that run's metadata, because a name arriving from the client would land in a
  // Content-Disposition header where a CR/LF is header injection.
  html_ok &= require(html.find("dmesg.txt") == std::string::npos, "the fixed dmesg.txt name survived");
  html_ok &= require(html.find("download=\"") == std::string::npos,
                     "the report still sets a download filename on an export control");
  html_ok &= require(html.find("id=\"export-dmesg\"") != std::string::npos, "the dmesg export control is missing");
  html_ok &= require(
      html.find("<button type=\"button\" class=\"export-pdf-btn\" id=\"export-dmesg\" disabled") != std::string::npos,
      "the dmesg control is not a real disabled button");

  // Notes render as their own callout, not as another monospace detail line.
  html_ok &=
      require(html.find("<div class=\"test-note\">") != std::string::npos, "notes are not rendered as a callout");
  html_ok &= require(html.find("only one pixel format") != std::string::npos, "note text is missing from the HTML");
  // The callout explains the data, so it has to come before the data it explains.
  const auto note_pos = html.find("<div class=\"test-note\">This device offers only one");
  const auto detail_pos = html.find("YUYV: sizeimage=4915200");
  html_ok &= require(note_pos != std::string::npos && detail_pos != std::string::npos && note_pos < detail_pos,
                     "the note callout should precede the detail list it explains");
  // Genuine omissions still report: t17's YUYV and t19's 3840x2160 both failed
  // S_FMT while other categories charted successfully.
  html_ok &= require(occurrence_count(html, "Not measured") == 2,
                     "expected two Not measured callouts (t17's YUYV and t19's 3840x2160), found " +
                         std::to_string(occurrence_count(html, "Not measured")));
  html_ok &= require(html.find("S_FMT failed") != std::string::npos, "a genuine omission reason went missing");
  // Regression: the single-format sweep charts nothing, so the heuristic has no
  // measured set to contrast against and must stay silent rather than flag the
  // one format it did measure.
  html_ok &= require(html.find("sizeimage=4915200</code>") == std::string::npos,
                     "a measured format is still being reported as Not measured");
  if (!html_ok)
    return 1;

  const std::string json = read_file(dir + "/" + artifact_name(result, v4l2diag::ReportFormat::Json));
  const std::string markdown = read_file(dir + "/" + artifact_name(result, v4l2diag::ReportFormat::Markdown));
  // Parsed rather than substring-matched: the artifact is now produced by the shared
  // serializer (jsoncpp), so the exact spacing is not the contract -- the structure is.
  Json::Value json_doc;
  {
    Json::CharReaderBuilder builder;
    std::string errors;
    std::istringstream in(json);
    if (!require(Json::parseFromStream(builder, in, &json_doc, &errors), "the JSON report does not parse: " + errors)) {
      return 1;
    }
  }
  const Json::Value &json_test = json_doc["cameras"][0]["tests"][0];
  if (!require(json_test["metrics"][0]["name"].asString() == "latency_mean", "JSON metrics changed unexpectedly") ||
      !require(markdown.find("| latency_mean |") != std::string::npos, "Markdown metrics changed unexpectedly")) {
    return 1;
  }

  bool text_ok = true;
  // Notes and warnings must survive into the machine-readable and text reports;
  // warnings used to be dropped from both entirely.
  text_ok &= require(json_test["notes"].isArray(), "JSON is missing the notes array");
  text_ok &= require(json.find("only one pixel format") != std::string::npos, "JSON is missing the note text");
  text_ok &= require(json_test["warnings"].isArray(), "JSON is missing the warnings array");
  // The artifact and the live API are one document now, so it carries the version.
  text_ok &= require(json_doc["result_schema_version"].asInt() == v4l2diag::kResultSchemaVersion,
                     "the JSON artifact carries no result schema version");
  text_ok &= require(markdown.find("> This device offers only one") != std::string::npos,
                     "Markdown should render notes as a blockquote");
  if (!text_ok)
    return 1;

  // --- Run-level trigger routing in all three formats ---------------------
  //
  // The routing is stated once per run. A camera's channel is found from its role
  // plus this table; repeating profile_id/trigger_channel_id per camera was how the
  // same fact ended up in several places.
  //
  // Parsed rather than substring-searched: a string search cannot tell WHERE a key
  // sits, so "profile_id is absent from the camera object" was really only checking
  // that the whole document never mentions it.
  bool routing_ok = true;
  Json::Value parsed;
  {
    Json::CharReaderBuilder builder;
    std::string errors;
    std::istringstream in(json);
    routing_ok &=
        require(Json::parseFromStream(builder, in, &parsed, &errors), "the JSON report does not parse: " + errors);
  }
  routing_ok &=
      require(parsed["trigger_profile_id"].asString() == "anvil", "JSON is missing the run-level trigger_profile_id");
  routing_ok &= require(parsed["trigger_mode"].asString() == "hardware", "JSON is missing the run-level trigger_mode");
  routing_ok &= require(parsed["role_bindings"].isArray() && parsed["role_bindings"].size() == 2,
                        "JSON is missing the run-level role_bindings table");
  if (parsed["role_bindings"].size() == 2) {
    routing_ok &= require(parsed["role_bindings"][0]["role"].asString() == "master" &&
                              parsed["role_bindings"][0]["trigger_channel_id"].asString() == "trigger-channel-a",
                          "the master binding is wrong or out of topology order");
    routing_ok &= require(parsed["role_bindings"][1]["role"].asString() == "slave-1" &&
                              parsed["role_bindings"][1]["trigger_channel_id"].asString() == "trigger-channel-b",
                          "the slave-1 binding is wrong or out of topology order");
  }
  // Timing is run-level too, and read from RunResult rather than derived from the
  // first camera -- these values exist nowhere else in the fixture.
  routing_ok &= require(parsed["trigger_rate_hz"].asDouble() == 12.5,
                        "JSON did not take trigger_rate_hz from the run-level field");
  routing_ok &=
      require(parsed["pulse_width_ms"].asDouble() == 3.25, "JSON did not take pulse_width_ms from the run-level field");

  // The camera object carries exactly the approved contract and none of the
  // run-level fields.
  routing_ok &=
      require(parsed["cameras"].isArray() && parsed["cameras"].size() == 1, "the JSON camera array is not as expected");
  if (parsed["cameras"].size() == 1) {
    const Json::Value &camera_json = parsed["cameras"][0];
    routing_ok &= require(camera_json["role"].asString() == "master", "the JSON camera entry does not carry its role");
    // "camera_path", not "path": the artifact and the API are one document now, and
    // the API's name is the canonical one (plan 2.6.2).
    routing_ok &=
        require(camera_json["camera_path"].asString() == "/dev/video-test", "the JSON camera entry lost its path");
    routing_ok &= require(!camera_json.isMember("path"), "the JSON camera entry still writes the legacy \"path\" key");
    routing_ok &= require(camera_json["tests"].isArray() && camera_json["tests"].size() > 0,
                          "the JSON camera entry lost its tests");
    // Each of these is a run-level fact; repeating it per camera is what let two
    // copies disagree.
    for (const char *field : {"profile_id", "trigger_channel_id", "trigger_mode", "trigger_profile_id", "role_bindings",
                              "trigger_rate_hz", "pulse_width_ms"}) {
      routing_ok &= require(!camera_json.isMember(field),
                            std::string("the JSON camera object still carries the run-level \"") + field + "\"");
    }
  }

  // Markdown: one routing table for the run.
  routing_ok &= require(markdown.find("### Trigger routing") != std::string::npos,
                        "Markdown is missing the Trigger routing table");
  routing_ok &= require(markdown.find("| Role | Trigger channel |") != std::string::npos,
                        "the Markdown routing table lost its header");
  routing_ok &= require(markdown.find("| master | trigger-channel-a |") != std::string::npos,
                        "the Markdown routing table lost the master row");
  routing_ok &= require(markdown.find("| slave-1 | trigger-channel-b |") != std::string::npos,
                        "the Markdown routing table lost the slave-1 row");
  routing_ok &= require(markdown.find("- Trigger profile: `anvil`") != std::string::npos,
                        "Markdown is missing the run-level trigger profile");
  routing_ok &= require(markdown.find("- Role: `master`") != std::string::npos,
                        "the Markdown camera section does not state its role");
  routing_ok &=
      require(markdown.find("- Profile: `") == std::string::npos, "Markdown still repeats the profile per camera");
  // Timing appears once, in the run header, not under each camera.
  routing_ok &= require(markdown.find("- Trigger rate: `12.50 Hz`") != std::string::npos,
                        "Markdown did not take the trigger rate from the run-level field");
  routing_ok &= require(markdown.find("- Pulse width: `3.25 ms`") != std::string::npos,
                        "Markdown did not take the pulse width from the run-level field");
  routing_ok &= require(html.find("12.50 Hz") != std::string::npos,
                        "HTML did not take the trigger rate from the run-level field");
  routing_ok &= require(markdown.find("- Trigger rate: `") < markdown.find("## Camera `"),
                        "the Markdown trigger rate is inside a camera section instead of the run header");

  // HTML: the same model.
  routing_ok &=
      require(html.find("Trigger Profile") != std::string::npos, "HTML is missing the run-level Trigger Profile row");
  routing_ok &= require(html.find(">anvil<") != std::string::npos, "HTML lost the trigger profile id");
  routing_ok &= require(html.find(">trigger-channel-a<") != std::string::npos, "HTML lost the master binding");
  routing_ok &= require(html.find(">slave-1<") != std::string::npos, "HTML lost the slave-1 role");
  routing_ok &= require(html.find(">Role<") != std::string::npos, "HTML does not label the camera's role");
  if (!routing_ok) {
    return 1;
  }

  // --- Write failures are never reported as success -----------------------
  //
  // "Every run writes HTML, JSON and Markdown" has to be a real guarantee: plan 2.6
  // reads the canonical JSON back after a restart, so a phantom artifact would turn
  // into a missing structured result. These previously all passed silently --
  // ensure_directory()'s result was ignored and the writers returned void.
  bool failure_ok = true;
  {
    // The output directory cannot be created: its parent is a regular file.
    const std::string base = make_temp_dir();
    const std::string blocker = base + "/not-a-dir";
    {
      std::ofstream out(blocker);
      out << "x";
    }
    bool threw = false;
    try {
      v4l2diag::write_reports(result, blocker + "/reports");
    } catch (const v4l2diag::ReportWriteError &error) {
      threw = true;
      failure_ok &= require(std::string(error.what()).find("output directory") != std::string::npos,
                            "the failure does not say the output directory could not be created");
    }
    failure_ok &= require(threw, "an uncreatable output directory was reported as a successful report");
    unlink(blocker.c_str());
    rmdir(base.c_str());
  }
  {
    // The directory exists but a mandatory artifact cannot be opened: a directory
    // already occupies the file's path.
    const std::string dir2 = make_temp_dir();
    failure_ok &= require(mkdir((dir2 + "/" + artifact_name(result, v4l2diag::ReportFormat::Json)).c_str(), 0755) == 0,
                          "could not stage an unwritable artifact path");
    bool threw = false;
    try {
      v4l2diag::write_reports(result, dir2);
    } catch (const v4l2diag::ReportWriteError &error) {
      threw = true;
      failure_ok &= require(std::string(error.what()).find("json") != std::string::npos,
                            "the failure does not name the artifact that could not be written");
    }
    failure_ok &= require(threw, "an unwritable mandatory artifact was reported as a successful report");
    // No phantom artifacts: nothing was returned at all, and the sibling files that
    // did get written are not advertised anywhere because the call threw.
    rmdir((dir2 + "/" + artifact_name(result, v4l2diag::ReportFormat::Json)).c_str());
    for (const auto format : {v4l2diag::ReportFormat::Html, v4l2diag::ReportFormat::Markdown}) {
      unlink((dir2 + "/" + artifact_name(result, format)).c_str());
    }
    rmdir(dir2.c_str());
  }
  {
    // A triggered run with no resolvable Trigger Profile file is REFUSED, not named with a
    // guess. Omitting the part would read as free-run; "default" would name a file nobody
    // chose. Either way a folder of archived runs would carry a name that looks right.
    v4l2diag::RunResult unnameable = result;
    unnameable.trigger_profile_file.clear();
    const std::string dir_unnameable = make_temp_dir();
    bool threw = false;
    std::string message;
    try {
      v4l2diag::write_reports(unnameable, dir_unnameable);
    } catch (const v4l2diag::ReportWriteError &error) {
      threw = true;
      message = error.what();
    }
    failure_ok &= require(threw, "a triggered run with no Trigger Profile file was written anyway");
    failure_ok &= require(message.find("Trigger Profile") != std::string::npos,
                          "the failure does not name the missing input: " + message);
    // Nothing was written: no half-named artifact is left behind.
    for (const auto format :
         {v4l2diag::ReportFormat::Html, v4l2diag::ReportFormat::Json, v4l2diag::ReportFormat::Markdown}) {
      failure_ok &= require(!exists(dir_unnameable + "/" + artifact_name(result, format)),
                            "a refused run still left an artifact behind");
    }
    rmdir(dir_unnameable.c_str());

    // Free-run is unaffected: it routes nothing, so it needs no profile.
    v4l2diag::RunResult free_run = result;
    free_run.trigger_mode = v4l2diag::TriggerMode::FreeRun;
    free_run.trigger_profile_file.clear();
    free_run.trigger_profile_id.clear();
    const std::string dir_free = make_temp_dir();
    bool free_ok = true;
    try {
      v4l2diag::write_reports(free_run, dir_free);
    } catch (const v4l2diag::ReportWriteError &error) {
      free_ok = require(false, std::string("a free-run report was refused: ") + error.what());
    }
    failure_ok &= free_ok;
    if (free_ok) {
      for (const auto format :
           {v4l2diag::ReportFormat::Html, v4l2diag::ReportFormat::Json, v4l2diag::ReportFormat::Markdown}) {
        unlink((dir_free + "/" + artifact_name(free_run, format)).c_str());
      }
    }
    rmdir(dir_free.c_str());
  }
  {
    // Positive control: in a writable directory all three files exist and are
    // readable, and every returned artifact points at a real file.
    const std::string dir3 = make_temp_dir();
    const auto three = v4l2diag::write_reports(result, dir3);
    failure_ok &= require(three.size() == 3, "a writable directory did not produce three artifacts");
    for (const auto &artifact : three) {
      failure_ok &= require(!read_file(artifact.path).empty(), "a returned artifact points at a missing or empty file");
    }
    for (const auto format :
         {v4l2diag::ReportFormat::Html, v4l2diag::ReportFormat::Json, v4l2diag::ReportFormat::Markdown}) {
      const std::string name = artifact_name(result, format);
      failure_ok &= require(!read_file(dir3 + "/" + name).empty(), "expected artifact is missing: " + name);
      unlink((dir3 + "/" + name).c_str());
    }
    rmdir(dir3.c_str());
  }
  if (!failure_ok) {
    return 1;
  }

  std::cout << dir << "\n";
  return 0;
}
