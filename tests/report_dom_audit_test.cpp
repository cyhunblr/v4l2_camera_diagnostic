// Automated HTML DOM audits (review-plan 6.12a) over a rendered report.
//
// 6.12 puts these checks on the DOM rather than on a PDF, because the product writes no
// PDF: page count, PDF signatures and link annotations are explicitly out of scope. What
// IS in scope is that the document a browser prints from is structurally sound.
//
// These ran ad hoc while sections 1-4 were implemented. Here they become a CTest target,
// so a later change that breaks an anchor or orphans a band fails the build instead of
// waiting for someone to look at a screenshot.

#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "v4l2diag/core/report_naming.hpp"
#include "v4l2diag/core/report_writer.hpp"
#include "v4l2diag/core/types.hpp"

namespace {

bool check(bool condition, const std::string &message) {
  if (!condition) {
    std::cout << "FAIL: " << message << "\n";
  }
  return condition;
}

bool contains(const std::string &haystack, const std::string &needle) {
  return haystack.find(needle) != std::string::npos;
}

std::size_t count_of(const std::string &haystack, const std::string &needle) {
  std::size_t count = 0;
  for (std::size_t at = haystack.find(needle); at != std::string::npos; at = haystack.find(needle, at + 1)) {
    ++count;
  }
  return count;
}

// Every attribute value of a given name, in document order.
std::vector<std::string> attributes(const std::string &html, const std::string &name) {
  std::vector<std::string> values;
  const std::string needle = name + "=\"";
  for (std::size_t at = html.find(needle); at != std::string::npos; at = html.find(needle, at + 1)) {
    const std::size_t start = at + needle.size();
    const std::size_t end = html.find('"', start);
    if (end == std::string::npos) {
      break;
    }
    values.push_back(html.substr(start, end - start));
  }
  return values;
}

std::string make_temp_dir() {
  char pattern[] = "/tmp/v4l2diag-dom-XXXXXX";
  const char *path = mkdtemp(pattern);
  return path == nullptr ? std::string() : std::string(path);
}

std::string read_file(const std::string &path) {
  std::ifstream in(path);
  std::ostringstream out;
  out << in.rdbuf();
  return out.str();
}

v4l2diag::ReportNaming naming_of(const v4l2diag::RunResult &run) {
  v4l2diag::ReportNaming naming;
  naming.started_at_utc = run.started_at_utc;
  naming.trigger_mode = run.trigger_mode;
  naming.trigger_profile_file = run.trigger_profile_file;
  naming.test_configuration_file = run.threshold_config_file;
  return naming;
}

v4l2diag::TestResult test_of(const std::string &id, const std::string &name, v4l2diag::TestStatus status,
                             const char *backend, double ms) {
  v4l2diag::TestResult test;
  test.id = id;
  test.name = name;
  test.status = status;
  test.memory_backend = backend;
  test.duration_ms = ms;
  test.summary = name + " completed.";
  test.category = "capture";
  v4l2diag::MetricValue frames;
  frames.name = "frames";
  frames.value = 240;
  frames.description = "Frames captured in this run.";
  test.metrics.push_back(frames);
  // A statistic family, so the tests whose previews approve a chart actually draw one.
  for (const char *suffix : {"_mean_ms", "_p95_ms", "_max_ms"}) {
    v4l2diag::MetricValue metric;
    metric.name = std::string("interval") + suffix;
    metric.value = 33.3;
    metric.unit = "ms";
    metric.description = "An interval statistic.";
    test.metrics.push_back(metric);
  }
  test.details.push_back("Backend: " + std::string(backend));
  return test;
}

// A multi-backend, multi-status run: the shape the audits need to say anything.
v4l2diag::RunResult audit_run() {
  v4l2diag::RunResult run;
  run.started_at_utc = "2026-08-04T12:02:38Z";
  run.finished_at_utc = "2026-08-04T12:41:07Z";
  run.host_name = "diag-host";
  run.kernel_release = "5.15.0-139-generic";
  run.kernel_version = "#1 SMP";
  run.threshold_config_file = "stress-test.json";
  run.trigger_mode = v4l2diag::TriggerMode::Hardware;
  run.trigger_profile_id = "anvil";
  run.trigger_profile_file = "anvil.json";
  run.run_id = "web-run-audit";

  v4l2diag::CameraRunResult camera;
  camera.camera_path = "/dev/video0";
  camera.role = "master";
  camera.trigger_description = "GPIO line 17";
  camera.tests.push_back(
      test_of("t01-device-compliance", "V4L2 Device Compliance", v4l2diag::TestStatus::Pass, "mmap", 1500));
  camera.tests.push_back(
      test_of("t23-sustained-capture", "Sustained Capture Stability", v4l2diag::TestStatus::Warn, "mmap", 600000));
  camera.tests.push_back(
      test_of("t12-dmabuf-cache-sync", "DMABUF CPU Read Synchronization", v4l2diag::TestStatus::Fail, "dmabuf", 7000));
  camera.tests.push_back(
      test_of("t11-memory-throughput", "Memory Access Throughput", v4l2diag::TestStatus::Skipped, "userptr", 0));
  run.cameras.push_back(camera);
  return run;
}

void remove_artifacts(const std::string &directory, const v4l2diag::RunResult &run) {
  for (const auto format :
       {v4l2diag::ReportFormat::Html, v4l2diag::ReportFormat::Json, v4l2diag::ReportFormat::Markdown}) {
    unlink((directory + "/" + v4l2diag::report_artifact_filename(naming_of(run), format)).c_str());
  }
  rmdir(directory.c_str());
}

}  // namespace

int main() {
  bool ok = true;

  const v4l2diag::RunResult run = audit_run();
  const std::string directory = make_temp_dir();
  bool wrote = true;
  try {
    v4l2diag::write_reports(run, directory);
  } catch (const std::exception &error) {
    wrote = check(false, std::string("write_reports threw: ") + error.what());
  }
  if (!wrote) {
    return 1;
  }
  const std::string html =
      read_file(directory + "/" + v4l2diag::report_artifact_filename(naming_of(run), v4l2diag::ReportFormat::Html));
  ok &= check(!html.empty(), "the report is empty");

  // --- 6.12a.1: every internal link resolves, exactly once -----------------
  {
    const std::vector<std::string> ids = attributes(html, "id");
    std::vector<std::string> targets;
    const std::string needle = "href=\"#";
    for (std::size_t at = html.find(needle); at != std::string::npos; at = html.find(needle, at + 1)) {
      const std::size_t start = at + needle.size();
      const std::size_t end = html.find('"', start);
      if (end == std::string::npos) {
        break;
      }
      targets.push_back(html.substr(start, end - start));
    }
    ok &= check(!targets.empty(), "the report has no internal links at all");
    for (const auto &target : targets) {
      const std::size_t defined = static_cast<std::size_t>(std::count(ids.begin(), ids.end(), target));
      ok &= check(defined == 1, "href=\"#" + target + "\" resolves to " + std::to_string(defined) + " targets");
    }
  }

  // --- 6.12a.2: anchors are result-<backend>-<test_id> and unique ----------
  {
    std::vector<std::string> anchors;
    for (const auto &id : attributes(html, "id")) {
      if (id.compare(0, 7, "result-") == 0) {
        anchors.push_back(id);
      }
    }
    ok &= check(anchors.size() == 4, "expected one anchor per test, got " + std::to_string(anchors.size()));
    for (const char *expected : {"result-mmap-t01-device-compliance", "result-mmap-t23-sustained-capture",
                                 "result-dmabuf-t12-dmabuf-cache-sync", "result-userptr-t11-memory-throughput"}) {
      ok &= check(std::find(anchors.begin(), anchors.end(), expected) != anchors.end(),
                  std::string("missing anchor ") + expected);
    }
    std::vector<std::string> sorted = anchors;
    std::sort(sorted.begin(), sorted.end());
    ok &= check(std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end(), "two cards share an anchor");
    // A camera path must never reach an anchor: the same test on two cameras would
    // otherwise produce two anchors for one design element.
    for (const auto &anchor : anchors) {
      // Match a device path, not the letters: "dev" also appears inside
      // "t01-device-compliance", which is a legitimate test slug.
      ok &= check(!contains(anchor, "video") && !contains(anchor, "-dev-") && !contains(anchor, "dev0"),
                  "a camera path leaked into the anchor " + anchor);
    }
  }

  // --- 6.12a.3: one backend band per group, in both sections ---------------
  {
    const std::size_t overview_at = html.find("Test Results Overview");
    const std::size_t detailed_at = html.find("<h2>Detailed Results</h2>");
    ok &= check(overview_at != std::string::npos && detailed_at != std::string::npos && overview_at < detailed_at,
                "the Overview and Detailed Results sections are missing or out of order");
    const std::string overview = html.substr(overview_at, detailed_at - overview_at);
    const std::string detailed = html.substr(detailed_at);
    // Three backends in this run: mmap, dmabuf, userptr.
    ok &= check(
        count_of(overview, "class=\"backend\"") == 3,
        "the Overview does not band each backend once, got " + std::to_string(count_of(overview, "class=\"backend\"")));
    ok &= check(count_of(detailed, "class=\"backend\"") == 3,
                "Detailed Results does not band each backend once, got " +
                    std::to_string(count_of(detailed, "class=\"backend\"")));
    // A band per GROUP, not per test: two mmap tests share one band.
    ok &= check(count_of(detailed, "class=\"backend\"") < count_of(detailed, "<article class=\"test-card"),
                "a backend band is repeated for every card");
  }

  // --- 6.12a.4: every chart is non-blank ---------------------------------
  {
    // A chart with an axis but no data element is worse than no chart: it looks like a
    // measurement that came out empty.
    const std::size_t charts = count_of(html, "class=\"metric-chart");
    ok &= check(charts > 0, "the report renders no charts at all");
    for (std::size_t at = html.find("<svg"); at != std::string::npos; at = html.find("<svg", at + 1)) {
      const std::size_t end = html.find("</svg>", at);
      if (end == std::string::npos) {
        break;
      }
      const std::string svg = html.substr(at, end - at);
      const bool has_mark = contains(svg, "<rect") || contains(svg, "<circle") || contains(svg, "<polyline") ||
                            contains(svg, "<path") || contains(svg, "<line");
      ok &= check(has_mark, "an SVG chart carries no data mark");
      ok &= check(contains(svg, "<text"), "an SVG chart carries no readable label");
      ok &= check(contains(svg, "viewBox"), "an SVG chart has no viewBox, so print cannot scale it");
    }
  }

  // --- 6.12a.5: extracted text order follows DOM order -------------------
  {
    // The Overview lists the tests in run order, and Detailed Results repeats that order.
    // A reader following a link back and forth otherwise finds the sections disagreeing.
    const std::vector<std::string> expected = {"T01", "T23", "T12", "T11"};
    const std::size_t detailed_at = html.find("<h2>Detailed Results</h2>");
    const std::string overview = html.substr(0, detailed_at);
    const std::string detailed = html.substr(detailed_at);
    std::size_t previous_overview = 0;
    std::size_t previous_detailed = 0;
    for (const auto &tag : expected) {
      const std::size_t in_overview = overview.find(tag + " - ");
      const std::size_t in_detailed = detailed.find(tag + " - ");
      ok &= check(in_overview != std::string::npos, tag + " is missing from the Overview");
      ok &= check(in_detailed != std::string::npos, tag + " is missing from Detailed Results");
      if (in_overview != std::string::npos && in_detailed != std::string::npos) {
        ok &= check(in_overview > previous_overview, "the Overview order does not follow run order at " + tag);
        ok &= check(in_detailed > previous_detailed, "the Detailed Results order does not follow run order at " + tag);
        previous_overview = in_overview;
        previous_detailed = in_detailed;
      }
    }
  }

  // --- 6.12a.6: the export controls are hidden in print ------------------
  {
    const std::size_t print_at = html.find("@media print");
    ok &= check(print_at != std::string::npos, "there is no print stylesheet");
    if (print_at != std::string::npos) {
      const std::string print_block = html.substr(print_at);
      ok &= check(contains(print_block, ".export-row, .export-note { display: none"),
                  "the export controls are not hidden in print");
    }
  }

  // --- 6.12a.7: exactly one end marker ----------------------------------
  {
    ok &= check(count_of(html, "class=\"footer\"") == 1,
                "expected exactly one end marker, got " + std::to_string(count_of(html, "class=\"footer\"")));
    // 6.9: the marker is normal document flow, not a running footer.
    // The marker must be normal flow. Checked on the footer RULE, because "position: fixed"
    // also appears in a CSS comment explaining why the toolbar no longer uses it.
    const std::size_t footer_css = html.find(".footer {");
    ok &= check(footer_css != std::string::npos, "the end marker has no style rule");
    if (footer_css != std::string::npos) {
      const std::string rule = html.substr(footer_css, html.find('}', footer_css) - footer_css);
      ok &= check(!contains(rule, "fixed") && !contains(rule, "absolute"),
                  "the end marker is taken out of document flow: " + rule);
    }
    ok &= check(!contains(html, "@bottom-center"), "the report uses an @page margin box for the footer");
  }

  // --- 6.12b.7: the @page A4 geometry is present ------------------------
  {
    // The remaining 6.12b checks need a real layout engine and live in the render harness
    // (docs/assets/source-render). The geometry declaration itself is checkable here.
    ok &= check(contains(html, "@page") && contains(html, "size: A4") && contains(html, "margin: 12mm 10mm 14mm"),
                "the approved @page geometry is missing");
  }

  remove_artifacts(directory, run);

  if (ok) {
    std::cout << "report_dom_audit_test: all checks passed\n";
  }
  return ok ? 0 : 1;
}
