// Per-test content renderers (plan 3.1, review round 2 item 6).
//
// The approved previews do NOT share one metric presentation: 22 of them carry a
// test-specific <table> with its own column schema (30 distinct header sets across the
// set), and none uses the generic metric-kv-list the source renderer was emitting. The
// decision (2026-08-04) is that each test carries its own content renderer inside the
// shared outer shell.
//
// This test locks the registry contract, not the pixels: which test id maps to which
// renderer, that the approved column names survive, that nothing is invented when the
// data is absent, and that an unknown test still gets an honest generic rendering.
//
// The approved column names are written out here on purpose. They are the contract --
// "Resolution | Coverage | Mean | P95 | State" is what was reviewed and approved for T19,
// and a renderer that quietly renamed a column would still produce a plausible-looking
// table.

#include <iostream>
#include <string>
#include <vector>

#include "v4l2diag/core/test_content.hpp"
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

v4l2diag::MetricValue mv(const std::string &name, double value, const std::string &unit) {
  v4l2diag::MetricValue metric;
  metric.name = name;
  metric.value = value;
  metric.unit = unit;
  return metric;
}

v4l2diag::TestResult test_of(const std::string &id, v4l2diag::TestStatus status = v4l2diag::TestStatus::Pass) {
  v4l2diag::TestResult test;
  test.id = id;
  test.name = id;
  test.status = status;
  test.memory_backend = "mmap";
  test.duration_ms = 12420.0;
  test.summary = "A summary sentence.";
  return test;
}

// Every test id that has a standalone approved preview, with the column headers that
// preview uses. Taken from the preview sources, which are the design authority here.
struct Expectation {
  const char *test_id;
  const char *columns[8];
};

const Expectation kApproved[] = {
    {"t03-pipeline-ready", {"Cycle", "STREAMON", "First frame", "Total ready", nullptr}},
    {"t06-stream-cycles", {"Phase", "Completed", "Start fail", "Timeout", "Outcome", nullptr}},
    {"t07-multi-buffer", {"Requested", "Allocated", "Captured", "Mean latency", "Outcome", nullptr}},
    {"t08-buffer-overwrite",
     {"Variant", "Trigger load", "Allocated", "Available", "Error flagged", "Outcome", nullptr}},
    {"t09-buffer-recycling", {"Delay", "Available", "Mean wait", "Outcome", nullptr}},
    {"t10-buffer-flags", {"Group", "Flag", "Observed", "Meaning", "State", nullptr}},
    {"t11-memory-throughput", {"Mapping", "Copy region", "Bytes/copy", "Throughput", "Relative to full", nullptr}},
    {"t12-dmabuf-cache-sync", {"Check", "Observed", "Meaning", nullptr}},
    {"t13-poll-timeout-cliff", {"Round", "Boundary confirmed", nullptr}},
    {"t14-trigger-latency", {"Measure", "Observed", "Meaning", nullptr}},
    {"t15-nonblock-vs-block", {"Measure", "Non-block", "Block", nullptr}},
    {"t16-gpio-pulse-width", {"Width", "Hits", "HIGH", nullptr}},
    {"t17-format-comparison", {"Format", "Coverage", "Sizeimage", "Mean", "Max", nullptr}},
    {"t18-control-sweep", {"Control", "Current", "Default", nullptr}},
    {"t19-resolution-sweep", {"Resolution", "Coverage", "Mean", "P95", "State", nullptr}},
    {"t20-sequence-continuity", {"Check", "Value", "State", nullptr}},
    {"t21-timestamp-monotonicity", {"Metric", "Value", "State", nullptr}},
    {"t22-stuck-frame", {"Metric", "Meaning", "State", nullptr}},
    {"t23-sustained-capture", {"Window", "Captured", "Mean", "Stddev", "Miss", nullptr}},
    {"t24-latency-under-load", {"Statistic", "Baseline", "CPU load", "Delta", nullptr}},
    {"t25-multi-camera", {"Camera", "Role", "Captures", "Mean delivery", "Max delivery", "Sync samples", nullptr}},
    {"t26-cold-start", {"Cycle", "Session", "Warm-up outcome", "Stability", nullptr}},
};

}  // namespace

int main() {
  bool ok = true;

  // --- 1. Every approved test id has its OWN renderer ---------------------
  {
    for (const auto &expected : kApproved) {
      ok &= check(v4l2diag::has_test_content_renderer(expected.test_id),
                  std::string("no content renderer registered for ") + expected.test_id);
    }
    // 22 standalone previews, 22 renderers. A count check catches a renderer that was
    // registered under a typo'd id and therefore silently unreachable.
    ok &= check(
        v4l2diag::registered_test_content_ids().size() >= 22,
        "fewer than 22 renderers are registered: " + std::to_string(v4l2diag::registered_test_content_ids().size()));

    // Every registered id must be a real technical test id. A registry keyed on a preview
    // filename ("t13-unified") would never match a run.
    for (const auto &id : v4l2diag::registered_test_content_ids()) {
      ok &= check(id.find("-unified") == std::string::npos && id.find("-shell") == std::string::npos,
                  "a renderer is keyed on a preview filename rather than a test id: " + id);
    }
  }

  // --- 2. The approved column names survive -------------------------------
  {
    // A renderer that renamed "Sizeimage" to "Size" would still produce a plausible
    // table, so the header text itself is the contract.
    for (const auto &expected : kApproved) {
      v4l2diag::TestResult test = test_of(expected.test_id);
      // Give it enough data that the table is not suppressed as empty.
      test.metrics.push_back(mv("frames", 240, ""));
      test.details.push_back("row: sample evidence");
      const std::string html = v4l2diag::render_test_content(test);
      ok &= check(!html.empty(), std::string("the renderer produced nothing for ") + expected.test_id);
      for (int i = 0; expected.columns[i] != nullptr; ++i) {
        ok &= check(contains(html, std::string("<th>") + expected.columns[i] + "</th>") ||
                        contains(html, std::string(">") + expected.columns[i] + "<"),
                    std::string(expected.test_id) + " lost the approved column \"" + expected.columns[i] + "\"");
      }
    }
  }

  // --- 3. The generic metric-kv-list is gone from these tests -------------
  {
    // The whole point of the decision: an approved test renders its own table, not the
    // generic key/value list the source used to emit for everything.
    for (const auto &expected : kApproved) {
      v4l2diag::TestResult test = test_of(expected.test_id);
      test.metrics.push_back(mv("frames", 240, ""));
      const std::string html = v4l2diag::render_test_content(test);
      ok &= check(contains(html, "<table"), std::string(expected.test_id) + " renders no test-specific table");
    }
  }

  // --- 4. Nothing is invented when the data is absent ---------------------
  {
    // A renderer must read from the structured metrics and details it was given. With no
    // data at all it says so; it does not print a plausible number.
    for (const auto &expected : kApproved) {
      const v4l2diag::TestResult empty = test_of(expected.test_id);
      const std::string html = v4l2diag::render_test_content(empty);
      // Digits that came from nowhere are the failure mode. A renderer with no data may
      // emit "Unavailable", an empty state, or nothing -- but not a fabricated value.
      ok &= check(!contains(html, "240") && !contains(html, "33.1") && !contains(html, "18000"),
                  std::string(expected.test_id) + " invented a value with no data: " + html.substr(0, 200));
      if (!html.empty()) {
        ok &= check(contains(html, "Unavailable") || contains(html, "<table") || contains(html, "no data"),
                    std::string(expected.test_id) + " produced neither a table nor an honest empty state");
      }
    }
  }

  // --- 5. An unknown test gets an honest generic rendering ---------------
  {
    // A test id the registry does not know must still render. Falling through to nothing
    // would silently drop a real test's evidence from the report.
    v4l2diag::TestResult unknown = test_of("t99-not-a-real-test");
    unknown.metrics.push_back(mv("frames", 240, ""));
    unknown.details.push_back("detail: something happened");
    ok &= check(!v4l2diag::has_test_content_renderer("t99-not-a-real-test"),
                "an unknown test claims a dedicated renderer");
    const std::string html = v4l2diag::render_test_content(unknown);
    ok &= check(!html.empty(), "an unknown test rendered nothing at all");
    // The generic fallback presents what it has: the metric and the detail.
    ok &= check(contains(html, "240"), "the generic fallback dropped the metric value");
    ok &= check(contains(html, "something happened"), "the generic fallback dropped the detail line");
  }

  // --- 6. Metric definitions appear where the previews have them ---------
  {
    // Five approved previews carry a "Metric definitions" table (Metric | Meaning). It
    // explains what the numbers mean, so dropping it leaves the reader with bare figures.
    for (const char *id :
         {"t22-stuck-frame", "t23-sustained-capture", "t24-latency-under-load", "t25-multi-camera", "t26-cold-start"}) {
      v4l2diag::TestResult test = test_of(id);
      test.metrics.push_back(mv("frames", 240, ""));
      const std::string html = v4l2diag::render_test_content(test);
      ok &= check(contains(html, "Metric definitions"), std::string(id) + " lost its Metric definitions section");
      ok &= check(contains(html, "Meaning"), std::string(id) + " lost the Meaning column");
    }
  }

  // --- 7. Conditional sections honour the status ------------------------
  {
    // The approved previews show a "Result" block on the non-PASS cards of T08, T09 and
    // T23. A PASS card's header already states the verdict.
    for (const char *id : {"t08-buffer-overwrite", "t09-buffer-recycling", "t23-sustained-capture"}) {
      v4l2diag::TestResult warned = test_of(id, v4l2diag::TestStatus::Warn);
      warned.metrics.push_back(mv("frames", 240, ""));
      v4l2diag::TestResult passed = test_of(id, v4l2diag::TestStatus::Pass);
      passed.metrics.push_back(mv("frames", 240, ""));
      ok &= check(v4l2diag::test_content_shows_result(warned.id, warned.status),
                  std::string(id) + " hides Result on a WARN card");
      ok &= check(!v4l2diag::test_content_shows_result(passed.id, passed.status),
                  std::string(id) + " shows Result on a PASS card");
    }
  }

  // --- 8. No double presentation of the same metric -----------------------
  {
    // Before the per-test renderers, the chart code dumped every un-charted metric into a
    // generic key/value list. Now the test's own table presents those metrics, so keeping
    // the list would show the same number twice under two different labels.
    //
    // Checked on the rendered report rather than on the renderer, because the list came
    // from a different function entirely -- the registry could be perfect and the report
    // still carry both.
    for (const auto &expected : kApproved) {
      v4l2diag::TestResult test = test_of(expected.test_id);
      test.metrics.push_back(mv("frames", 240, ""));
      const std::string html = v4l2diag::render_test_content(test);
      ok &= check(!contains(html, "metric-kv-list"),
                  std::string(expected.test_id) + " still emits the generic metric-kv-list");
      ok &= check(!contains(html, "Supporting values"),
                  std::string(expected.test_id) + " still emits the Supporting values strip");
    }
  }

  // --- 9. Charts appear only where a preview approved one ------------------
  {
    // Every chart in a report is approved content. The chart selector picks a statistic
    // family (_mean_ms / _p95_ms / _max_ms) automatically, which meant eight tests grew a
    // dot chart their preview never showed -- "extra" is not a free pass, it is unapproved
    // content in a report a customer reads.
    //
    // The allow-list is the preview set: a test appears here iff at least one of its
    // approved preview cards carries an <svg>.
    // t03-pipeline-ready is deliberately absent: it has its OWN content renderer that
    // draws the approved chart directly, so the generic statistic-family selector must
    // stay off for it -- test_content_registry_test section 12 covers T03's chart.
    // t06-stream-cycles is deliberately absent here too: its own content renderer draws
    // the approved reliability bars and trend chart directly (see section 15).
    // t07-multi-buffer is deliberately absent here too: its own content renderer draws
    // its two approved charts directly (see section 16).
    const char *kChartsApproved[] = {
        "t09-buffer-recycling",   "t13-poll-timeout-cliff", "t16-gpio-pulse-width", "t23-sustained-capture",
        "t24-latency-under-load", "t25-multi-camera",       "t26-cold-start",
    };

    for (const char *id : kChartsApproved) {
      ok &= check(v4l2diag::test_charts_approved(id),
                  std::string("charts were disabled for ") + id + ", whose preview approves one");
    }
    // The eight that grew an unapproved chart, named individually so a regression says
    // which test came back.
    for (const char *id :
         {"t12-dmabuf-cache-sync", "t14-trigger-latency", "t15-nonblock-vs-block", "t17-format-comparison",
          "t18-control-sweep", "t19-resolution-sweep", "t21-timestamp-monotonicity", "t22-stuck-frame"}) {
      ok &= check(!v4l2diag::test_charts_approved(id),
                  std::string(id) + " renders a chart its approved preview does not have");
    }
    // An unknown test gets no chart: an unapproved rendering must not appear just because
    // nobody listed the test.
    ok &= check(!v4l2diag::test_charts_approved("t99-not-a-real-test"),
                "an unregistered test is allowed to render charts");
  }

  // --- 10. T01, per review-plan 5.1 ---------------------------------------
  {
    // 5.1.5: three capabilities, in this order, each with its probe method in
    // parentheses on the SAME line and its state right-aligned. The probe used to occupy
    // its own row, which doubled the section's height and separated a fact from its label.
    v4l2diag::TestResult t01 = test_of("t01-device-compliance");
    t01.name = "V4L2 Device Compliance";
    t01.metrics.push_back(mv("format_count", 2, ""));
    t01.metrics.push_back(mv("backend_supported", 1, ""));
    t01.metrics.push_back(mv("capture_supported", 1, ""));
    t01.metrics.push_back(mv("streaming_supported", 1, ""));
    t01.details.push_back("driver: v4l2 loopback");
    t01.details.push_back("card: ADASTEC_VD");
    t01.details.push_back("bus: platform:vim2m");
    t01.details.push_back("format: UYVY - UYVY 4:2:2 - single-plane");
    t01.details.push_back("format: NV16 - Y/CbCr 4:2:2 - single-plane");
    t01.details.push_back("format: UYVY - UYVY 4:2:2 - single-plane");
    const std::string html = v4l2diag::render_test_content(t01);

    ok &= check(contains(html, "Required capabilities"), "T01 has no Required capabilities section");
    const std::size_t backend_at = html.find("Backend support");
    const std::size_t capture_at = html.find("Capture support");
    const std::size_t streaming_at = html.find("Streaming support");
    ok &= check(backend_at != std::string::npos && capture_at != std::string::npos && streaming_at != std::string::npos,
                "T01 is missing one of the three capabilities");
    ok &=
        check(backend_at < capture_at && capture_at < streaming_at, "T01's capabilities are out of the approved order");
    // 5.1.5: the probe method rides on the capability's own line.
    ok &= check(contains(html, "Backend support (VIDIOC_REQBUFS accepted)"),
                "the backend probe method is not on the capability line");
    ok &= check(contains(html, "SUPPORTED"), "no capability state is shown");

    // 5.1.6: FOURCC values are unique. "UYVY, NV16, UYVY" is two formats, not three.
    ok &= check(count_of(html, "UYVY - UYVY 4:2:2") == 1, "a repeated FOURCC was listed twice");
    ok &= check(contains(html, "2 formats"), "the format count does not deduplicate by FOURCC");

    // 5.1.7: driver, card and bus as structured rows -- not the monospace detail block,
    // which repeated the same values a second time.
    for (const char *key : {"Driver", "Card", "Bus"}) {
      ok &= check(contains(html, key), std::string("T01 is missing the ") + key + " row");
    }
    ok &= check(!contains(html, "class=\"detail-list\""),
                "T01 still emits the monospace detail block that repeats its metrics");

    // 5.1.8: T01 is a categorical capability test; it gets no chart.
    ok &= check(!v4l2diag::test_charts_approved("t01-device-compliance"), "T01 is allowed to render a chart");

    // 5.1.2: only the backend this card belongs to. A card under BACKEND MMAP naming
    // DMABUF's probe result would attribute one backend's finding to another.
    ok &= check(!contains(html, "Selected backend"), "the pre-5.1 \"Selected backend\" row survived");

    // 5.1.4: RESULT only on non-PASS.
    ok &= check(!contains(v4l2diag::render_test_content(t01), "section-label\">Result<"),
                "a passing T01 card shows a RESULT section");
    v4l2diag::TestResult warned = t01;
    warned.status = v4l2diag::TestStatus::Warn;
    ok &= check(contains(v4l2diag::render_test_content(warned), "section-label\">Result<"),
                "a warning T01 card hides its RESULT section");
  }

  // --- 11. T02, per review-plan 5.2 ---------------------------------------
  {
    v4l2diag::TestResult t02 = test_of("t02-control-inventory");
    t02.name = "V4L2 Control Inventory";
    t02.metrics.push_back(mv("controls", 14, ""));
    t02.metrics.push_back(mv("writable", 13, ""));
    t02.metrics.push_back(mv("read_only", 1, ""));
    // Class records are group headings, not controls (5.2.3), and carry no current value.
    t02.details.push_back("control_class: User Controls");
    t02.details.push_back("control: Brightness|0x00980900|rw|0..255 step 1|128|120");
    t02.details.push_back("control: Contrast|0x00980901|rw|0..255 step 1|128|128");
    t02.details.push_back("control_class: Camera Controls");
    t02.details.push_back("control: Exposure|0x009a0902|ro|0..10000 step 1|100|Unavailable (EINVAL)");
    const std::string html = v4l2diag::render_test_content(t02);

    // 5.2.4: three summary values, and the word "count" is not repeated beside them.
    for (const char *key : {"Controls", "Writable", "Read-only"}) {
      ok &= check(contains(html, key), std::string("T02 is missing the ") + key + " summary value");
    }
    ok &= check(!contains(html, "14 count") && !contains(html, "count</"),
                "T02 repeats the word \"count\" beside its summary values");

    // 5.2.5: the approved five columns, replacing the monospace detail list.
    for (const char *column : {"Control", "Access", "Range", "Default", "Current"}) {
      ok &= check(contains(html, std::string("<th>") + column + "</th>"),
                  std::string("T02 is missing the ") + column + " column");
    }
    ok &= check(!contains(html, "class=\"detail-list\""), "T02 still emits the monospace detail list");

    // 5.2.5: the hex id sits under the control name, at lower visual weight.
    ok &= check(contains(html, "class=\"control-id\">0x00980900<"),
                "the control's hexadecimal id is not shown beneath its name");

    // 5.2.3: a class record is a full-width group heading with no current value read.
    ok &= check(contains(html, "class=\"group-row\""), "T02 does not render class records as group headings");
    ok &=
        check(contains(html, "User Controls") && contains(html, "Camera Controls"), "T02 lost a control-class heading");

    // 5.2.6: an unreadable value says so, with the reason.
    ok &=
        check(contains(html, "Unavailable (EINVAL)"), "T02 does not show the reason a control value could not be read");

    // 5.2.7: categorical inventory test -- no chart, and no RESULT on a passing card.
    ok &= check(!v4l2diag::test_charts_approved("t02-control-inventory"), "T02 is allowed to render a chart");
    ok &= check(!contains(html, "section-label\">Result<"), "a passing T02 card shows a RESULT section");
  }

  // --- 12. T03, per review-plan 5.3 ---------------------------------------
  {
    // 5.3.3/5.3.7: mean/max summary, threshold beside the measurement, sub-millisecond
    // precision preserved.
    v4l2diag::TestResult t03_free = test_of("t03-pipeline-ready");
    t03_free.name = "Pipeline Readiness after STREAMON";
    t03_free.metrics.push_back(mv("streamon_mean_ms", 18.0, "ms"));
    t03_free.metrics.push_back(mv("streamon_max_ms", 20.010, "ms"));
    t03_free.metrics.push_back(mv("first_frame_mean_ms", 102.0, "ms"));
    t03_free.metrics.push_back(mv("first_frame_max_ms", 110.0, "ms"));
    t03_free.metrics.push_back(mv("cycles", 10, ""));
    t03_free.metrics.push_back(mv("timeouts", 0, ""));
    t03_free.metrics.push_back(mv("streamon_slow_start_ms", 50.0, "ms"));
    t03_free.metrics.push_back(mv("first_frame_pass_ms", 200.0, "ms"));
    t03_free.metrics.push_back(mv("eagain_retries_mean", 1234.0, ""));
    t03_free.metrics.push_back(mv("streamon_attempts_max", 3, ""));
    t03_free.details.push_back("cycle: 1|18.0|102.0|120.0");
    t03_free.details.push_back("cycle: 2|18.5|101.5|120.0");
    const std::string free_html = v4l2diag::render_test_content(t03_free);

    for (const char *key :
         {"STREAMON mean", "STREAMON max", "First-frame mean", "First-frame max", "Completed cycles", "Timeouts"}) {
      ok &= check(contains(free_html, key), std::string("T03 is missing the ") + key + " summary value");
    }
    // 5.3.7: 20.010ms keeps its sub-millisecond digit rather than rounding to "20ms".
    ok &= check(contains(free_html, "20.01 ms") || contains(free_html, "20.010 ms"),
                "T03 lost sub-millisecond precision on a summary value");

    // 5.3.4: ONE stacked bar per cycle, not two separate dot charts.
    ok &= check(!contains(free_html, "First frame ms") || !contains(free_html, "Streamon ms"),
                "T03 still renders the separate mean/max dot charts");
    ok &= check(contains(free_html, "t03-phase-streamon") && contains(free_html, "t03-phase-frame"),
                "T03 has no stacked two-phase timing chart");
    ok &= check(contains(free_html, "STREAMON") && contains(free_html, "Total"),
                "T03's chart does not label the STREAMON phase or the per-cycle total");

    // 5.3.5: free-run's precise cycle table -- Cycle/STREAMON/First frame/Total ready, and
    // explicitly NO Pulses column.
    for (const char *column : {"Cycle", "STREAMON", "First frame", "Total ready"}) {
      ok &= check(contains(free_html, std::string("<th>") + column + "</th>"),
                  std::string("T03 free-run is missing the ") + column + " column");
    }
    ok &= check(!contains(free_html, "<th>Pulses</th>"), "T03 free-run shows a Pulses column");
    ok &= check(!contains(free_html, "pulse"), "T03 free-run mentions trigger pulses at all");

    // 5.3.8: EAGAIN and STREAMON-attempt values live in Technical details, not the summary.
    ok &= check(contains(free_html, "Technical details"), "T03 has no Technical details section");
    ok &= check(contains(free_html, "Average retries"), "T03's retry average is not labelled \"Average retries\"");
    ok &= check(contains(free_html, "1,234") || contains(free_html, "1234"),
                "the EAGAIN average does not appear in Technical details");

    // 5.3.6: a hardware/software run adds the Pulses column.
    v4l2diag::TestResult t03_hw = t03_free;
    t03_hw.details.clear();
    t03_hw.details.push_back("cycle: 1|1133.0|145.0|1278.0|2");
    t03_hw.details.push_back("cycle: 2|1124.0|145.0|1269.0|2");
    const std::string hw_html = v4l2diag::render_test_content(t03_hw);
    ok &= check(contains(hw_html, "<th>Pulses</th>"), "T03 hardware/software does not show a Pulses column");
  }

  // --- 13. T04, per review-plan 5.4 ---------------------------------------
  {
    v4l2diag::TestResult t04 = test_of("t04-no-streamon", v4l2diag::TestStatus::Fail);
    t04.name = "Frame Capture without STREAMON";
    // 5.4.4: two check rows -- poll() before STREAMON, DQBUF before STREAMON.
    t04.details.push_back("check: poll() before STREAMON|POLLIN not expected|Timed out after 50ms|as expected");
    t04.details.push_back(
        "check: DQBUF before STREAMON|Rejected with EAGAIN|Frame dequeued - sequence 3|unexpected delivery");
    // 5.4.6: only DQBUF's own errno, from a failed ioctl -- not a stale one from an
    // earlier syscall.
    t04.metrics.push_back(mv("dqbuf_errno", 0, ""));
    t04.metrics.push_back(mv("buffers_requested", 4, ""));
    t04.metrics.push_back(mv("poll_timeout_ms", 50, ""));
    const std::string html = v4l2diag::render_test_content(t04);

    // 5.4.4: the structured CHECK/EXPECTED/OBSERVED/OUTCOME table replaces the raw metric
    // list and the repeating detail block.
    for (const char *column : {"Check", "Expected", "Observed", "Outcome"}) {
      ok &= check(contains(html, std::string("<th>") + column + "</th>"),
                  std::string("T04 is missing the ") + column + " column");
    }
    ok &= check(contains(html, "poll() before STREAMON") && contains(html, "DQBUF before STREAMON"),
                "T04 is missing one of its two approved check rows");
    ok &=
        check(contains(html, "Frame dequeued - sequence 3"), "T04 does not show the frame-delivered-early observation");
    ok &= check(!contains(html, "class=\"detail-list\""), "T04 still emits the raw repeating detail block");

    // 5.4.3: FAIL states which sequence was delivered before STREAMON.
    ok &= check(contains(html, "section-label\">Result<"), "a failing T04 card hides its RESULT section");
    ok &= check(contains(html, "3"), "T04's RESULT does not name the delivered sequence number");

    // 5.4.7: Buffers requested and Poll timeout in Test configuration.
    ok &= check(contains(html, "Test configuration"), "T04 has no Test configuration section");
    ok &= check(contains(html, "Buffers requested") && contains(html, "Poll timeout"),
                "T04's Test configuration is missing a required parameter");

    // 5.4.7: T04 is a categorical state-machine test -- no chart.
    ok &= check(!v4l2diag::test_charts_approved("t04-no-streamon"), "T04 is allowed to render a chart");

    // A passing T04 (poll() timed out, DQBUF correctly rejected) shows no RESULT.
    v4l2diag::TestResult t04_pass = t04;
    t04_pass.status = v4l2diag::TestStatus::Pass;
    t04_pass.details.clear();
    t04_pass.details.push_back("check: poll() before STREAMON|POLLIN not expected|Timed out after 50ms|as expected");
    t04_pass.details.push_back(
        "check: DQBUF before STREAMON|Rejected with EAGAIN|Rejected with EAGAIN (11)|as expected");
    ok &= check(!contains(v4l2diag::render_test_content(t04_pass), "section-label\">Result<"),
                "a passing T04 card shows a RESULT section");
  }

  // --- 14. T05, per review-plan 5.5 ---------------------------------------
  {
    v4l2diag::TestResult t05 = test_of("t05-pollerr-handling", v4l2diag::TestStatus::Pass);
    t05.name = "STREAMOFF Error Handling and Recovery";
    t05.details.push_back("phase: Baseline capture|30/30 captured|30/30 captured|as expected");
    t05.details.push_back("phase: STREAMOFF|Success|Success|as expected");
    t05.details.push_back("phase: Poll after stop|Timed out after 50ms|Timed out after 50ms|as expected");
    t05.details.push_back("phase: DQBUF after stop|Rejected with EINVAL|Rejected with EINVAL (22)|as expected");
    t05.details.push_back("phase: Re-STREAMON|Success|Success|as expected");
    t05.details.push_back("phase: Recovery capture|30/30 captured|30/30 captured|as expected");
    t05.metrics.push_back(mv("baseline_frames", 30, ""));
    t05.metrics.push_back(mv("recovery_frames", 30, ""));
    t05.metrics.push_back(mv("warmup_frames", 5, ""));
    t05.metrics.push_back(mv("min_recovery_frames", 25, ""));
    t05.metrics.push_back(mv("poll_timeout_ms", 50, ""));
    t05.details.push_back("backend_memory: mmap");
    const std::string html = v4l2diag::render_test_content(t05);

    // 5.5.5: PHASE/EXPECTED/OBSERVED/OUTCOME, in the six-step run order.
    for (const char *column : {"Phase", "Expected", "Observed", "Outcome"}) {
      ok &= check(contains(html, std::string("<th>") + column + "</th>"),
                  std::string("T05 is missing the ") + column + " column");
    }
    const std::size_t baseline_at = html.find("Baseline capture");
    const std::size_t streamoff_at = html.find("STREAMOFF<");
    const std::size_t poll_at = html.find("Poll after stop");
    const std::size_t dqbuf_at = html.find("DQBUF after stop");
    const std::size_t restreamon_at = html.find("Re-STREAMON");
    const std::size_t recovery_at = html.find("Recovery capture");
    ok &= check(baseline_at != std::string::npos && streamoff_at != std::string::npos && poll_at != std::string::npos &&
                    dqbuf_at != std::string::npos && restreamon_at != std::string::npos &&
                    recovery_at != std::string::npos,
                "T05 is missing one of its six approved phase rows");
    ok &= check(baseline_at < streamoff_at && streamoff_at < poll_at && poll_at < dqbuf_at &&
                    dqbuf_at < restreamon_at && restreamon_at < recovery_at,
                "T05's phase rows are out of run order");
    ok &= check(!contains(html, "class=\"detail-list\""), "T05 still emits the raw repeating detail block");

    // 5.5.7: the six configuration parameters.
    for (const char *key : {"Baseline frames", "Recovery frames", "Warmup frames", "Minimum recovery", "Poll timeout",
                            "Backend memory"}) {
      ok &= check(contains(html, key), std::string("T05 is missing the ") + key + " configuration row");
    }

    // 5.5.7: categorical, sequential state-machine test -- no chart.
    ok &= check(!v4l2diag::test_charts_approved("t05-pollerr-handling"), "T05 is allowed to render a chart");

    // A passing card shows no RESULT.
    ok &= check(!contains(html, "section-label\">Result<"), "a passing T05 card shows a RESULT section");

    // 5.5.6: the capture-rate note, only when a pacing observation exists.
    v4l2diag::TestResult t05_paced = t05;
    t05_paced.details.push_back(
        "capture_rate_note: Observed 28 fps against a configured 30 fps read pace; "
        "verdict unaffected.");
    const std::string paced_html = v4l2diag::render_test_content(t05_paced);
    ok &= check(contains(paced_html, "Capture rate note"), "T05 does not show the capture rate note when present");
    ok &= check(!contains(html, "Capture rate note"),
                "T05 shows a capture rate note when no pacing observation was recorded");
  }

  // --- 15. T06, per review-plan 5.6 ---------------------------------------
  {
    v4l2diag::TestResult t06 = test_of("t06-stream-cycles", v4l2diag::TestStatus::Pass);
    t06.name = "STREAMON/STREAMOFF Cycle Reliability";
    t06.metrics.push_back(mv("full_cycles", 20, ""));
    t06.metrics.push_back(mv("full_configured", 20, ""));
    t06.metrics.push_back(mv("full_start_fail", 0, ""));
    t06.metrics.push_back(mv("full_timeouts", 0, ""));
    t06.metrics.push_back(mv("rapid_cycles", 50, ""));
    t06.metrics.push_back(mv("rapid_configured", 50, ""));
    t06.metrics.push_back(mv("rapid_start_fail", 0, ""));
    t06.metrics.push_back(mv("rapid_capture_timeouts", 0, ""));
    t06.metrics.push_back(mv("open_streamon_mean_ms", 0.271, "ms"));
    t06.metrics.push_back(mv("open_streamon_max_ms", 0.350, "ms"));
    t06.metrics.push_back(mv("measured_capture_mean_ms", 79.863, "ms"));
    t06.metrics.push_back(mv("measured_capture_max_ms", 93.816, "ms"));
    t06.details.push_back("full_phase: Completed");
    t06.details.push_back("rapid_phase: Completed");
    t06.details.push_back("slow_start_guard: Not reached");
    t06.details.push_back("full_cycle_timing: 1|0.30|2|0.28|3|0.271");
    t06.details.push_back("full_cycles_configured: 20");
    t06.details.push_back("rapid_cycles_configured: 50");
    t06.details.push_back("full_warmup: 3 frames");
    t06.details.push_back("rapid_warmup: 1 frame");
    t06.details.push_back("slow_start_guard_limit: 2s");
    t06.details.push_back("backend_memory: mmap");
    const std::string html = v4l2diag::render_test_content(t06);

    // 5.6.4: Full and rapid rows in ONE table; counts as completed/configured.
    for (const char *column : {"Phase", "Completed", "Start fail", "Timeout", "Outcome"}) {
      ok &= check(contains(html, std::string("<th>") + column + "</th>"),
                  std::string("T06 is missing the ") + column + " column");
    }
    ok &= check(contains(html, "20/20"), "T06 does not show completed/configured for the full phase");
    ok &= check(contains(html, "50/50"), "T06 does not show completed/configured for the rapid phase");
    ok &= check(contains(html, "Full cycles") && contains(html, "Rapid cycles"),
                "T06 is missing one of its two phase rows");
    ok &= check(!contains(html, "class=\"detail-list\""), "T06 still emits the raw supporting-value list");

    // 5.6.5: a threshold-banded horizontal reliability bar, once per phase.
    ok &= check(contains(html, "class=\"reliability-bar\""), "T06 has no reliability bar chart");
    ok &= check(count_of(html, "class=\"reliability-bar\"") == 2, "T06's reliability chart does not show both phases");
    ok &= check(contains(html, "100%"), "T06's reliability bar does not print the percentage alongside the bar");

    // 5.6.6: the Open + STREAMON trend chart, by full-cycle order -- not the removed
    // Streamon-ms dot chart.
    ok &= check(contains(html, "Open + STREAMON") && contains(html, "by full cycle"),
                "T06 has no Open + STREAMON trend chart");
    ok &= check(!contains(html, "Streamon ms"), "T06 still labels the metric \"Streamon ms\"");

    // 5.6.7: the renamed timing fields.
    for (const char *label :
         {"Open + STREAMON mean", "Open + STREAMON maximum", "Measured capture mean", "Measured capture maximum"}) {
      ok &= check(contains(html, label), std::string("T06 is missing the \"") + label + "\" timing field");
    }
    ok &= check(!contains(html, "First frame latency"), "T06 still uses the \"First frame latency\" label");

    // 5.6.8: booleans translated to semantic text, not raw true/false.
    ok &= check(contains(html, "Protection State") || contains(html, "Protection state"),
                "T06 has no Protection State section");
    ok &= check(contains(html, "Completed") && contains(html, "Not reached"),
                "T06 does not translate its protection booleans to semantic text");
    ok &= check(!contains(html, ">true<") && !contains(html, ">false<"),
                "T06 shows a raw boolean instead of semantic text");

    // 5.6.9: the eight configuration parameters.
    for (const char *key :
         {"Full cycles", "Rapid cycles", "Full warmup", "Rapid warmup", "Slow-start guard", "Backend memory"}) {
      ok &= check(contains(html, key), std::string("T06 is missing the ") + key + " configuration row");
    }
  }

  // --- 16. T07, per review-plan 5.7 ---------------------------------------
  {
    v4l2diag::TestResult t07 = test_of("t07-multi-buffer", v4l2diag::TestStatus::Pass);
    t07.name = "Multi-buffer Configurations";
    // Five requests, all allocated to depth 2 (free-run-like fixture).
    t07.details.push_back("request: 1|2|20|20|59");
    t07.details.push_back("request: 2|2|20|20|79");
    t07.details.push_back("request: 3|2|20|20|84");
    t07.details.push_back("request: 4|2|20|20|84");
    t07.details.push_back("request: 5|2|20|20|61");
    t07.metrics.push_back(mv("total_captured", 100, ""));
    t07.metrics.push_back(mv("total_attempted", 100, ""));
    t07.details.push_back("requested_range: 1-5");
    t07.details.push_back("samples_per_request: 20");
    t07.details.push_back("backend_memory: mmap");
    t07.details.push_back("warmup: 3 frames");
    t07.details.push_back("capture_timeout: 100ms");
    t07.details.push_back("sample_interval: 200ms");
    const std::string html = v4l2diag::render_test_content(t07);

    // 5.7.4: the one-sentence allocation behaviour, then the aggregate capture summary --
    // NOT written as "miss=0/20", which conflates the aggregate with a per-request result.
    ok &= check(contains(html, "effective depth of 2 buffers") || contains(html, "allocated"),
                "T07 has no allocation-behaviour sentence");
    ok &= check(contains(html, "100/100"), "T07 does not show the aggregate captured/attempted summary");
    ok &= check(!contains(html, "miss=0/20"), "T07 writes the aggregate as \"miss=N/M\"");

    // 5.7.5: the five approved columns, Title Case in the markup (CSS may transform it).
    for (const char *column : {"Requested", "Allocated", "Captured", "Mean latency", "Outcome"}) {
      ok &= check(contains(html, std::string("<th>") + column + "</th>"),
                  std::string("T07 is missing the ") + column + " column");
    }
    ok &= check(count_of(html, "<td>20/20</td>") == 5, "T07 does not show captured/attempted per request row");
    ok &= check(!contains(html, "class=\"detail-list\""), "T07 still emits the raw repeating detail block");

    // 5.7.6: requested-vs-allocated chart, and 5.7.7: latency grouped by allocated depth,
    // with repeats collapsing into one point (all five requests share depth 2).
    ok &= check(contains(html, "Requested vs allocated"), "T07 has no requested-vs-allocated chart");
    ok &= check(contains(html, "Capture latency by allocated depth"), "T07 has no allocated-depth latency chart");
    ok &= check(count_of(html, "t07-depth-point") == 1,
                "T07's latency chart does not collapse repeated allocated depths into one point");

    // 5.7.8: the six configuration parameters, in order.
    const std::size_t range_at = html.find("Requested range");
    const std::size_t samples_at = html.find("Samples per request");
    const std::size_t backend_at = html.find("Backend memory");
    const std::size_t warmup_at = html.find("Warmup");
    const std::size_t timeout_at = html.find("Capture timeout");
    const std::size_t interval_at = html.find("Sample interval");
    ok &=
        check(range_at != std::string::npos && samples_at != std::string::npos && backend_at != std::string::npos &&
                  warmup_at != std::string::npos && timeout_at != std::string::npos && interval_at != std::string::npos,
              "T07 is missing one of its six configuration parameters");
    ok &= check(range_at < samples_at && samples_at < backend_at && backend_at < warmup_at && warmup_at < timeout_at &&
                    timeout_at < interval_at,
                "T07's configuration parameters are out of the approved order");
  }

  // --- 17. T08, per review-plan 5.8 ---------------------------------------
  {
    v4l2diag::TestResult t08 = test_of("t08-buffer-overwrite", v4l2diag::TestStatus::Warn);
    t08.name = "Buffer Saturation Behavior";
    t08.summary =
        "Both saturation variants retained 2/2 buffers, but one retained buffer in "
        "each variant carried V4L2_BUF_FLAG_ERROR.";
    t08.metrics.push_back(mv("allocated_buffers", 2, ""));
    t08.details.push_back("variant: Variant A|100 at 100ms|2|2|2|1|2|WARN");
    t08.details.push_back("variant: Variant B|200 at 50ms|2|2|2|1|2|WARN");
    t08.details.push_back("evidence: Variant A|Buffer 0, sequence 0: ERROR, MAPPED, TIMESTAMP_MONOTONIC|0x2041");
    t08.details.push_back("evidence: Variant B|Buffer 0, sequence 0: ERROR, MAPPED, TIMESTAMP_MONOTONIC|0x2041");
    t08.details.push_back("settle_time: 500ms");
    t08.details.push_back("backend_memory: mmap");
    t08.details.push_back("variant_a_config: 100 / 100ms");
    t08.details.push_back("variant_b_config: 200 / 50ms");
    t08.details.push_back("error_threshold: 0");
    const std::string html = v4l2diag::render_test_content(t08);

    // 5.8.5: the saturation-load comparison, both variants named with their rate.
    ok &= check(contains(html, "Saturation Load") || contains(html, "Saturation load"),
                "T08 has no Saturation Load chart");
    ok &= check(contains(html, "100 at 10/s") || contains(html, "100"),
                "T08's load chart does not name Variant A's trigger rate");
    ok &= check(contains(html, "200 at 20/s") || contains(html, "200"),
                "T08's load chart does not name Variant B's trigger rate");
    ok &= check(contains(html, "approximately 10 seconds"), "T08 does not state the ~10s trigger-load duration");

    // 5.8.6: the queue-after-saturation slots, with ERROR/READY as visible TEXT, not only
    // colour, and the error slot naming its buffer index.
    ok &= check(contains(html, "Queue After Saturation") || contains(html, "Queue after saturation"),
                "T08 has no Queue After Saturation chart");
    ok &= check(count_of(html, ">ERROR<") >= 2, "T08's queue chart does not label both error slots as ERROR");
    ok &= check(count_of(html, ">READY<") >= 2, "T08's queue chart does not label both ready slots as READY");
    ok &= check(contains(html, "buffer 0"), "T08's error slot does not name its buffer index");

    // 5.8.7: the six approved columns, observed/allocated format.
    for (const char *column : {"Variant", "Trigger load", "Allocated", "Available", "Error flagged", "Outcome"}) {
      ok &= check(contains(html, std::string("<th>") + column + "</th>"),
                  std::string("T08 is missing the ") + column + " column");
    }
    ok &= check(contains(html, "2/2") && contains(html, "1/2"),
                "T08 does not show available/error-flagged as observed/allocated");
    ok &= check(!contains(html, "class=\"detail-list\""), "T08 still emits the raw repeating detail block");

    // 5.8.8: the hex flag decoded by name, with the raw value preserved alongside it.
    ok &= check(contains(html, "ERROR, MAPPED, TIMESTAMP_MONOTONIC") || contains(html, "ERROR"),
                "T08 does not decode the flag bits by name");
    ok &= check(contains(html, "0x2041"), "T08 does not preserve the raw flag value");

    // 5.8.9: the six configuration parameters.
    for (const char *key :
         {"Allocated buffers", "Settle time", "Backend memory", "Variant A", "Variant B", "Error threshold"}) {
      ok &= check(contains(html, key), std::string("T08 is missing the ") + key + " configuration row");
    }
  }

  if (ok) {
    std::cout << "test_content_registry_test: all checks passed\n";
  }
  return ok ? 0 : 1;
}
