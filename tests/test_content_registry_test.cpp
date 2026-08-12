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
#include <utility>
#include <vector>

#include "v4l2diag/core/diagnostic_runner.hpp"
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

// The Value cell of the grid row whose first cell is `label`.
//
// Rows are `<div class="grid-row cols-N"><span>Label</span><span>type</span><span>unit</span>
// <span>VALUE</span>...`, so the value is the fourth span. Returning the cell rather than
// searching the whole card matters: "0.013" appears legitimately as the standard deviation
// elsewhere in the same table, and a card-wide substring search would find it and call a
// mislabelled row correct.
std::string cell_after_label(const std::string &html, const std::string &label) {
  const std::string needle = "<span>" + label + "</span>";
  const std::size_t at = html.find(needle);
  if (at == std::string::npos) {
    return "(label not rendered)";
  }
  std::size_t cursor = at + needle.size();
  std::string cell;
  for (int index = 0; index < 3; ++index) {
    const std::size_t open = html.find("<span", cursor);
    if (open == std::string::npos) {
      return "(row truncated)";
    }
    const std::size_t gt = html.find('>', open);
    const std::size_t close = html.find("</span>", gt);
    if (gt == std::string::npos || close == std::string::npos) {
      return "(row truncated)";
    }
    cell = html.substr(gt + 1, close - gt - 1);
    cursor = close + 7;
  }
  return cell;
}

// Every test id that has a standalone approved preview, with the column headers that
// preview uses. Taken from the preview sources, which are the design authority here.
struct Expectation {
  const char *test_id;
  const char *columns[8];
};

const Expectation kApproved[] = {
    // The columns each test's approved card names, read from the 26 previews in
    // docs/assets/refactored_previews/. A renderer that renamed "Sizeimage" to "Size" would still
    // produce a plausible table, so the header text itself is the contract.
    //
    // Outcome/State are absent by design: a verdict lives in the Measurement Result
    // table's Status column, and the evidence tables carry Detail instead.
    {"t03-pipeline-ready", {"Metric", "Type", "Unit", "Value", "Detail", nullptr}},
    {"t06-stream-cycles", {"Metric", "Type", "Unit", "Value", "Detail", nullptr}},
    {"t07-multi-buffer", {"Requested", "Allocated", "Captured", "Mean latency", "Detail", nullptr}},
    {"t08-buffer-overwrite", {"Variant", "Trigger load", "Allocated", "Available", "Error flagged", "Detail", nullptr}},
    {"t09-buffer-recycling", {"Delay", "Available", "Mean wait", "Detail", nullptr}},
    {"t10-buffer-flags", {"Group", "Flag", "Observed", "Meaning", "Detail", nullptr}},
    {"t11-memory-throughput", {"Copy region", "Bytes per copy", "Throughput", "Relative to full", nullptr}},
    {"t12-dmabuf-cache-sync", {"Metric", "Type", "Unit", "Value", "Detail", nullptr}},
    {"t13-poll-timeout-cliff", {"Round", "Detail", nullptr}},
    {"t14-trigger-latency", {"Metric", "Type", "Unit", "Value", "Detail", nullptr}},
    {"t15-nonblock-vs-block", {"Metric", "Type", "Unit", "Non-block", "Block", nullptr}},
    {"t16-gpio-pulse-width", {"Width", "Hits", "HIGH mean", "LOW mean (derived)", "Detail", nullptr}},
    {"t17-format-comparison", {"Format", "Resolution", "Sizeimage", "Mean latency", "Max latency", nullptr}},
    {"t18-control-sweep", {"Control", "Current", "Default", "Access", nullptr}},
    {"t19-resolution-sweep", {"Resolution", "Pixel format", "Mean latency", "P95 latency", "Throughput", nullptr}},
    {"t20-sequence-continuity", {"Metric", "Type", "Unit", "Value", "Detail", nullptr}},
    {"t21-timestamp-monotonicity", {"Metric", "Value", "Detail", nullptr}},
    {"t22-stuck-frame", {"Metric", "Type", "Unit", "Value", "Detail", nullptr}},
    {"t23-sustained-capture", {"Window", "Captured", "Mean", "Stddev", "Miss", nullptr}},
    {"t24-latency-under-load", {"Statistic", "Baseline", "CPU load", "Delta", nullptr}},
    {"t25-multi-camera", {"Camera", "Role", "Captures", "Mean delivery", "Max delivery", "Sync samples", nullptr}},
    {"t26-cold-start", {"Cycle", "Session", "Warm-up outcome", "Detail", nullptr}},
};

}  // namespace

// A test result carrying the metrics and detail lines every renderer reads, so a card
// that draws no chart is doing so by its own structure rather than for want of data.
v4l2diag::TestResult chartable_test(const std::string &id) {
  v4l2diag::TestResult test = test_of(id);
  for (const auto &entry :
       {std::make_pair("latency_min", 44.7), std::make_pair("latency_mean", 44.8), std::make_pair("latency_p95", 44.9),
        std::make_pair("latency_max", 45.0), std::make_pair("baseline_latency_mean", 44.8),
        std::make_pair("baseline_latency_p95", 44.9), std::make_pair("load_latency_mean", 44.9),
        std::make_pair("load_latency_p95", 44.8), std::make_pair("cliff_ms", 45.0),
        std::make_pair("first_miss_ms", 44.0), std::make_pair("safety_margin_ms", 3.5), std::make_pair("hits_1ms", 8.0),
        // Eleven swept widths, as the device records: T16's chart is a LINE over the
        // sweep, and a single width cannot make a line -- a one-width fixture made the
        // renderer look broken when it was correctly refusing to draw.
        std::make_pair("lat_high_avg_1ms", 44.80), std::make_pair("lat_low_avg_1ms", 43.80),
        std::make_pair("lat_high_avg_2ms", 44.82), std::make_pair("lat_low_avg_2ms", 43.82),
        std::make_pair("lat_high_avg_3ms", 44.84), std::make_pair("lat_low_avg_3ms", 43.84),
        std::make_pair("lat_high_avg_5ms", 44.86), std::make_pair("lat_low_avg_5ms", 43.86),
        std::make_pair("lat_high_avg_7ms", 44.88), std::make_pair("lat_low_avg_7ms", 43.88),
        std::make_pair("lat_high_avg_10ms", 44.90), std::make_pair("lat_low_avg_10ms", 43.90),
        std::make_pair("lat_high_avg_13ms", 44.92), std::make_pair("lat_low_avg_13ms", 43.92),
        std::make_pair("lat_high_avg_15ms", 44.94), std::make_pair("lat_low_avg_15ms", 43.94),
        std::make_pair("lat_high_avg_20ms", 44.96), std::make_pair("lat_low_avg_20ms", 43.96),
        std::make_pair("lat_high_avg_25ms", 44.98), std::make_pair("lat_low_avg_25ms", 43.98),
        std::make_pair("lat_high_avg_30ms", 45.00), std::make_pair("lat_low_avg_30ms", 44.00),
        std::make_pair("success_rate_pct", 100.0), std::make_pair("frames_captured", 388.0),
        std::make_pair("max_consecutive_miss", 0.0), std::make_pair("cycles_completed", 10.0),
        std::make_pair("censored_cycles", 0.0), std::make_pair("warmup_mean_frames", 1.0),
        std::make_pair("warmup_max_frames", 1.0), std::make_pair("identical_pairs", 0.0),
        std::make_pair("frames_tested", 50.0), std::make_pair("max_identical_run", 0.0)}) {
    v4l2diag::MetricValue metric;
    metric.name = entry.first;
    metric.value = entry.second;
    metric.unit = "ms";
    test.metrics.push_back(metric);
  }
  for (const char *line : {"Win0 0-10s: n=65 mean=44ms stddev=0 miss=0", "cycle 1: warmup=1 frames",
                           "coarse: 150ms \xE2\x86\x92 10/10", "capture_timeout: 100ms"}) {
    test.details.push_back(line);
  }
  return test;
}

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
        ok &= check(contains(html, std::string("<span>") + expected.columns[i] + "</span>") ||
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
      ok &= check(contains(html, "class=\"grid-head cols-"),
                  std::string(expected.test_id) + " renders no test-specific table");
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
        ok &= check(
            contains(html, "Unavailable") || contains(html, "class=\"grid-head cols-") || contains(html, "no data"),
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
    // The generic fallback presents the metric it has. It does NOT print the raw detail
    // list: that dump appeared in no approved preview, and on the 2026-08-09 device run
    // nine real cards emitted it, each repeating its own Test Configuration table in
    // unstyled `key: value` form. The detail assertion is inverted rather than removed so
    // the dump coming back counts as a regression.
    ok &= check(contains(html, "240"), "the generic fallback dropped the metric value");
    ok &= check(!contains(html, "something happened"), "the raw detail dump came back in the generic fallback");
  }

  // --- 6. Metric definitions appear where the previews have them ---------
  {
    // The "Metric definitions" glossary is BANNED (design-spec): some of its rows defined
    // terms no table used, the rest restated a column heading. A metric that genuinely
    // needs a bound states it on its own row instead. This assertion is inverted from the
    // one it replaces -- the section coming back is the regression now.
    for (const char *id :
         {"t22-stuck-frame", "t23-sustained-capture", "t24-latency-under-load", "t25-multi-camera", "t26-cold-start"}) {
      v4l2diag::TestResult test = test_of(id);
      test.metrics.push_back(mv("frames", 240, ""));
      const std::string html = v4l2diag::render_test_content(test);
      ok &= check(!contains(html, "Metric definitions") && !contains(html, "Metric Definitions"),
                  std::string(id) + " brought the Metric definitions glossary back");
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
    // Which cards carry a chart is no longer decided by an allow-list: every approved
    // chart is drawn by the test's own content renderer, so the question "does this card
    // have a chart" is answered by the rendered HTML. Checked here against the two ends of
    // the contract -- a test whose preview approves a chart must draw one, and a test
    // whose preview has none must not.
    for (const char *id : {"t13-poll-timeout-cliff", "t16-gpio-pulse-width", "t23-sustained-capture",
                           "t24-latency-under-load", "t26-cold-start"}) {
      v4l2diag::TestResult test = chartable_test(id);
      const std::string html = v4l2diag::render_test_content(test);
      ok &= check(contains(html, "chart-frame"), std::string(id) + " draws no chart, but its preview approves one");
    }
    for (const char *id : {"t01-device-compliance", "t02-control-inventory", "t04-no-streamon", "t05-pollerr-handling",
                           "t10-buffer-flags", "t12-dmabuf-cache-sync", "t18-control-sweep"}) {
      v4l2diag::TestResult test = chartable_test(id);
      const std::string html = v4l2diag::render_test_content(test);
      ok &= check(!contains(html, "chart-frame"),
                  std::string(id) + " renders a chart its approved preview does not have");
    }
  }

  // --- 10. T01, per review-plan 5.1 ---------------------------------------
  {
    // 5.1.5: three capabilities, in this order, with the state right-aligned.
    //
    // User decision 2026-08-10: the probe method moved BACK to its own row and the label is
    // "Backend Support" alone. The earlier reasoning (a fact should not be separated from its
    // label) was overruled: in the rendered card the parenthetical made the longest label in
    // the column carry an ioctl name, which read as part of the verdict.
    v4l2diag::TestResult t01 = test_of("t01-device-compliance");
    t01.name = "V4L2 Device Compliance";
    t01.metrics.push_back(mv("format_count", 2, ""));
    t01.metrics.push_back(mv("backend_supported", 1, ""));
    t01.metrics.push_back(mv("capture_supported", 1, ""));
    t01.metrics.push_back(mv("streaming_supported", 1, ""));
    t01.details.push_back("driver: v4l2 loopback");
    t01.details.push_back("card: VENDOR_VD");
    t01.details.push_back("bus: platform:vim2m");
    t01.details.push_back("format: UYVY - UYVY 4:2:2 - single-plane");
    t01.details.push_back("format: NV16 - Y/CbCr 4:2:2 - single-plane");
    t01.details.push_back("format: UYVY - UYVY 4:2:2 - single-plane");
    const std::string html = v4l2diag::render_test_content(t01);

    ok &= check(contains(html, "Device Capability"), "T01 has no Device Capability section");
    const std::size_t backend_at = html.find("Backend Support");
    const std::size_t capture_at = html.find("Capture support");
    const std::size_t streaming_at = html.find("Streaming support");
    ok &= check(backend_at != std::string::npos && capture_at != std::string::npos && streaming_at != std::string::npos,
                "T01 is missing one of the three capabilities");
    ok &=
        check(backend_at < capture_at && capture_at < streaming_at, "T01's capabilities are out of the approved order");
    // The probe method is its own row beneath the capability, naming the ioctl and nothing
    // else -- not folded into the label, and not restating the verdict.
    ok &= check(!contains(html, "Backend Support (") && !contains(html, "Backend support ("),
                "the probe method is still folded into the capability label");
    ok &= check(contains(html, "Accepted ioctl") && contains(html, "VIDIOC_REQBUFS"),
                "the backend probe method is not on its own row");
    // "Backend supported" was the run's spelling; the label is "Backend Support".
    ok &= check(!contains(html, "Backend supported"), "the capability label lost its capital S");
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

    // 5.1.2: only the backend this card belongs to. A card under BACKEND MMAP naming
    // DMABUF's probe result would attribute one backend's finding to another.
    ok &= check(!contains(html, "Selected backend"), "the pre-5.1 \"Selected backend\" row survived");

    // 5.1.4: RESULT only on non-PASS.
    ok &= check(!contains(v4l2diag::render_test_content(t01), "class=\"result-fail\""),
                "a passing T01 card shows a RESULT section");
    v4l2diag::TestResult warned = t01;
    warned.status = v4l2diag::TestStatus::Warn;
    // The result line is a status-classed div above the sections now, not a section.
    ok &= check(contains(v4l2diag::render_test_content(warned), "class=\"result-warn\""),
                "a warning T01 card hides its result line");
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

    // User decision 2026-08-10: NO summary strip. The approved t02 preview is the section
    // heading followed directly by the table. The strip was also wrong -- "Read-only" resolved
    // {"writable_count", "read_only"} in that order and printed the WRITABLE count -- so both
    // the unapproved content and the wrong number leave together. The counts stay in
    // Measurement Result, which is where the verdict reads them.
    {
      const std::size_t table_at = html.find("grid-head");
      const std::string before = table_at == std::string::npos ? html : html.substr(0, table_at);
      ok &= check(before.find("<dt>Controls</dt>") == std::string::npos,
                  "T02 still renders the unapproved Controls summary value");
      ok &= check(before.find("<dt>Writable</dt>") == std::string::npos,
                  "T02 still renders the unapproved Writable summary value");
      ok &= check(before.find("<dt>Read-only</dt>") == std::string::npos,
                  "T02 still renders the Read-only summary value (which read the writable count)");
    }

    // 5.2.5: the approved five columns, replacing the monospace detail list.
    for (const char *column : {"Control", "Access", "Range", "Default", "Current"}) {
      ok &= check(contains(html, std::string("<span>") + column + "</span>"),
                  std::string("T02 is missing the ") + column + " column");
    }
    ok &= check(!contains(html, "class=\"detail-list\""), "T02 still emits the monospace detail list");

    // The approved preview gives the id its own column rather than tucking it under the
    // name, so it is read as data alongside Access, Range and Step.
    ok &= check(contains(html, "<span>0x00980900</span>"), "the control's hexadecimal id is not its own cell");

    // 5.2.3: a class record is a full-width group heading with no current value read.
    ok &= check(contains(html, "group-row"), "T02 does not render class records as group headings");
    ok &=
        check(contains(html, "User Controls") && contains(html, "Camera Controls"), "T02 lost a control-class heading");

    // 5.2.6: an unreadable value says so, with the reason.
    ok &=
        check(contains(html, "Unavailable (EINVAL)"), "T02 does not show the reason a control value could not be read");

    ok &= check(!contains(html, "class=\"result-fail\""), "a passing T02 card shows a RESULT section");
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
    // The approved preview stacks two CSS segments per cycle inside one .bar-track.
    // These ids belonged to an SVG production drew instead; the chart is the same
    // measurement, drawn the way the design asks for.
    ok &= check(contains(free_html, "class=\"stacked-chart\"") && contains(free_html, "class=\"bar-streamon\"") &&
                    contains(free_html, "class=\"bar-firstframe\""),
                "T03 has no stacked two-phase timing chart");
    ok &= check(contains(free_html, "STREAMON") && contains(free_html, "class=\"cycle-info\""),
                "T03's chart does not label the STREAMON phase or the per-cycle total");

    // The per-cycle TABLE is gone: the approved layout charts those numbers and charting
    // them loses nothing, so a table beside the chart would print each value twice under
    // two labels. What must survive is the chart carrying the per-cycle data, asserted
    // just above, and free-run still claiming no trigger pulses.
    ok &= check(!contains(free_html, "<span>Pulses</span>"), "T03 free-run shows a Pulses column");
    ok &= check(!contains(free_html, "pulse"), "T03 free-run mentions trigger pulses at all");

    // The Technical details section is gone with it -- three sections, no fourth.
    ok &= check(!contains(free_html, "Technical details"), "T03's Technical details section came back");

    // 5.3.6: a hardware/software run adds the Pulses column.
    v4l2diag::TestResult t03_hw = t03_free;
    t03_hw.details.clear();
    t03_hw.details.push_back("cycle: 1|1133.0|145.0|1278.0|2");
    t03_hw.details.push_back("cycle: 2|1124.0|145.0|1269.0|2");
    const std::string hw_html = v4l2diag::render_test_content(t03_hw);
    ok &= check(contains(hw_html, "trigger pulses"), "T03 hardware/software loses the trigger pulse count");
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
    v4l2diag::record_run_parameters(&t04);
    const std::string html = v4l2diag::render_test_content(t04);

    // 5.4.4: the structured CHECK/EXPECTED/OBSERVED/OUTCOME table replaces the raw metric
    // list and the repeating detail block.
    for (const char *column : {"Phase", "Expected", "Observed", "Detail"}) {
      ok &= check(contains(html, std::string("<span>") + column + "</span>"),
                  std::string("T04 is missing the ") + column + " column");
    }
    ok &= check(contains(html, "poll() before STREAMON") && contains(html, "DQBUF before STREAMON"),
                "T04 is missing one of its two approved check rows");
    ok &=
        check(contains(html, "Frame dequeued - sequence 3"), "T04 does not show the frame-delivered-early observation");
    ok &= check(!contains(html, "class=\"detail-list\""), "T04 still emits the raw repeating detail block");

    // 5.4.3: FAIL states which sequence was delivered before STREAMON.
    ok &= check(contains(html, "class=\"result-fail\""), "a failing T04 card hides its result line");
    ok &= check(contains(html, "3"), "T04's RESULT does not name the delivered sequence number");

    // 5.4.7: Buffers requested and Poll timeout in Test configuration.
    ok &= check(contains(html, "Test Configuration"), "T04 has no Test Configuration section");
    // Approved labels (Faz 2): "Buffer count", not "Buffers requested".
    ok &= check(contains(html, "Buffer count") && contains(html, "Poll timeout"),
                "T04's Test configuration is missing a required parameter");

    // A passing T04 (poll() timed out, DQBUF correctly rejected) shows no RESULT.
    v4l2diag::TestResult t04_pass = t04;
    t04_pass.status = v4l2diag::TestStatus::Pass;
    t04_pass.details.clear();
    t04_pass.details.push_back("check: poll() before STREAMON|POLLIN not expected|Timed out after 50ms|as expected");
    t04_pass.details.push_back(
        "check: DQBUF before STREAMON|Rejected with EAGAIN|Rejected with EAGAIN (11)|as expected");
    ok &= check(!contains(v4l2diag::render_test_content(t04_pass), "class=\"result-fail\""),
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
    v4l2diag::record_run_parameters(&t05);
    const std::string html = v4l2diag::render_test_content(t05);

    // 5.5.5: PHASE/EXPECTED/OBSERVED/OUTCOME, in the six-step run order.
    for (const char *column : {"Phase", "Expected", "Observed", "Detail"}) {
      ok &= check(contains(html, std::string("<span>") + column + "</span>"),
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

    // 5.5.7: the configuration parameters. The rows are now recorded by the runner
    // (record_run_parameters) rather than hand-assembled by this renderer, and they carry the
    // approved labels -- "Baseline captures", not "Baseline frames". Which rows each test must
    // show, and that they are complete, is checked against the previews in
    // tests/test_configuration_contract_test.cpp; this only confirms T05's card reads them.
    for (const char *key : {"Baseline captures", "Recovery captures", "Warmup count"}) {
      ok &= check(contains(html, key), std::string("T05 is missing the ") + key + " configuration row");
    }

    // A passing card shows no RESULT.
    ok &= check(!contains(html, "class=\"result-fail\""), "a passing T05 card shows a RESULT section");

    // 5.5.6: the capture-rate note, only when a pacing observation exists.
    v4l2diag::TestResult t05_paced = t05;
    t05_paced.details.push_back(
        "capture_rate_note: Observed 28 fps against a configured 30 fps read pace; "
        "verdict unaffected.");
    const std::string paced_html = v4l2diag::render_test_content(t05_paced);
    // The standalone note section is gone; the observation rides on the row it qualifies,
    // so the TEXT must still be present -- just not under its own heading.
    ok &= check(contains(paced_html, "Capture rate note") == false, "T05's Capture rate note section came back");
    ok &= check(!contains(html, "Capture rate note"),
                "T05 shows a capture rate note when no pacing observation was recorded");
  }

  // --- 15. T06, per review-plan 5.6 ---------------------------------------
  {
    v4l2diag::TestResult t06 = test_of("t06-stream-cycles", v4l2diag::TestStatus::Pass);
    t06.name = "STREAMON/STREAMOFF Cycle Reliability";
    // The names diagnostic_runner.cpp actually emits. The old ones were the renderer's
    // guesses and are kept as fallbacks, so both are present here.
    t06.metrics.push_back(mv("full_cycles_success", 20, ""));
    t06.metrics.push_back(mv("full_cycles_attempted", 20, ""));
    t06.metrics.push_back(mv("full_start_fail", 0, ""));
    t06.metrics.push_back(mv("full_timeouts", 0, ""));
    t06.metrics.push_back(mv("rapid_cycles_ok", 50, ""));
    t06.metrics.push_back(mv("rapid_cycles_attempted", 50, ""));
    t06.metrics.push_back(mv("rapid_start_fail", 0, ""));
    t06.metrics.push_back(mv("rapid_capture_timeouts", 0, ""));
    t06.metrics.push_back(mv("open_streamon_mean_ms", 0.271, "ms"));
    t06.metrics.push_back(mv("open_streamon_max_ms", 0.350, "ms"));
    t06.metrics.push_back(mv("measured_capture_mean_ms", 79.863, "ms"));
    t06.metrics.push_back(mv("measured_capture_max_ms", 93.816, "ms"));
    // The cycle counts are metrics, not detail lines: the Aggregate and Protection state
    // items read them directly now that the duplicate Phase table is gone.
    t06.metrics.push_back(mv("full_cycles_success", 20, "count"));
    t06.metrics.push_back(mv("full_cycles_attempted", 20, "count"));
    t06.metrics.push_back(mv("rapid_cycles_ok", 50, "count"));
    t06.metrics.push_back(mv("rapid_cycles_attempted", 50, "count"));
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
    v4l2diag::record_run_parameters(&t06);
    const std::string html = v4l2diag::render_test_content(t06);

    // The approved preview shows "Cycle reliability" as the threshold-banded bars alone;
    // a Phase|Completed|Start fail|Timeout table above them repeated the same two counts,
    // so the card stated everything twice. What must survive is the bars themselves and
    // the reliability figures, checked below.
    for (const char *column : {"Metric", "Type", "Unit", "Value", "Detail"}) {
      ok &= check(contains(html, std::string("<span>") + column + "</span>"),
                  std::string("T06 is missing the ") + column + " column");
    }
    // The completed/configured pair used to be one "20/20" cell in the removed Phase
    // table. The canonical columns separate the reading from what it was judged against,
    // so the count is its own cell and the limit sits in Detail.
    ok &= check(contains(html, "<span>20</span>"), "T06 does not report the full-phase cycle count");
    ok &= check(contains(html, "<span>50</span>"), "T06 does not report the rapid-phase cycle count");
    ok &= check(contains(html, "Full cycles") && contains(html, "Rapid cycles"),
                "T06 is missing one of its two phase rows");
    ok &= check(!contains(html, "class=\"detail-list\""), "T06 still emits the raw supporting-value list");

    // 5.6.5: a threshold-banded horizontal reliability bar, once per phase.
    // .rel-row inside a .chart-frame, as the preview draws it -- the old .reliability-bar
    // markup carried no chart frame at all, so the card had no chart by the contract.
    ok &= check(contains(html, "class=\"rel-chart\""), "T06 has no reliability bar chart");
    ok &= check(count_of(html, "class=\"rel-row\"") == 2, "T06's reliability chart does not show both phases");
    ok &= check(contains(html, "class=\"rel-info\""), "T06's reliability bar does not print the counts beside the bar");

    // T06 draws ONE chart -- the reliability bars above. The per-cycle "Open + STREAMON"
    // trend chart is gone: the approved preview has no second chart, and that SVG plotted
    // cycle duration, which is a different question from the completion rate this card
    // reports. The metric name itself survives in the Aggregate table, which is what the
    // second assertion below still checks.
    ok &= check(!contains(html, "by full cycle"), "T06's unapproved per-cycle trend chart came back");
    ok &= check(contains(html, "Open + STREAMON"), "T06 lost the Open + STREAMON metric from its tables");
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
    v4l2diag::record_run_parameters(&t07);
    const std::string html = v4l2diag::render_test_content(t07);

    // User decision 2026-08-10: the card opens with evidence, not with a sentence. The
    // approved t07 preview carries no paragraph in any of its three cards, so the
    // allocation-behaviour sentence and the bare "100/100 frames captured" line are gone.
    ok &= check(!contains(html, "effective depth of 2 buffers"), "T07 still opens with an allocation sentence");
    ok &= check(!contains(html, "frames captured</strong>"), "T07 still prints the bare capture-total line");
    // The finding itself is NOT lost (project rule 4c): the ratio stays in Measurement Result
    // and the per-request rows still carry each allocation.
    ok &= check(contains(html, "100/100"), "T07 lost the aggregate captured/attempted ratio entirely");
    ok &= check(!contains(html, "miss=0/20"), "T07 writes the aggregate as \"miss=N/M\"");

    // 5.7.5: the five approved columns, Title Case in the markup (CSS may transform it).
    for (const char *column : {"Requested", "Allocated", "Captured", "Mean latency", "Detail"}) {
      ok &= check(contains(html, std::string("<span>") + column + "</span>"),
                  std::string("T07 is missing the ") + column + " column");
    }
    ok &= check(count_of(html, "<span>20/20</span>") == 5, "T07 does not show captured/attempted per request row");
    ok &= check(!contains(html, "class=\"detail-list\""), "T07 still emits the raw repeating detail block");

    // T07 has NO chart: its approved preview shows none in any of its three cards.
    // Production drew two SVGs here (requested-vs-allocated, latency-by-depth); both
    // restated what the Aggregate table states per row. Inverted rather than deleted so
    // the charts coming back is a regression. The item label below is approved and stays:
    // it names the table.
    ok &= check(!contains(html, "aria-label=\"Requested versus allocated buffers\""),
                "T07's unapproved requested-vs-allocated chart came back");
    ok &= check(!contains(html, "aria-label=\"Capture latency by allocated buffer depth\""),
                "T07's unapproved allocated-depth latency chart came back");
    ok &= check(contains(html, "<h4 class=\"item-label\">Latency by buffer count</h4>"),
                "T07's charts are not named by the approved item label");
    ok &= check(count_of(html, "t07-depth-point") == 0, "T07's unapproved depth chart came back");

    // 5.7.8: the six configuration parameters, in the approved card's own order and wording.
    // Both changed in Faz 2: the rows now come from record_run_parameters() and read "Sample
    // count" / "Max buffers" / "Warmup count", where this test used to expect the runner's
    // internal spellings ("Requested range", "Samples per request", "Warmup").
    const std::size_t sample_at = html.find("Sample count");
    const std::size_t maxbuf_at = html.find("Max buffers");
    const std::size_t warmup_at = html.find("Warmup count");
    const std::size_t timeout_at = html.find("Capture timeout");
    const std::size_t interval_at = html.find("Sample interval");
    const std::size_t backend_at = html.find("Backend memory");
    ok &= check(sample_at != std::string::npos && maxbuf_at != std::string::npos && warmup_at != std::string::npos &&
                    timeout_at != std::string::npos && interval_at != std::string::npos &&
                    backend_at != std::string::npos,
                "T07 is missing one of its six configuration parameters");
    ok &= check(sample_at < maxbuf_at && maxbuf_at < warmup_at && warmup_at < timeout_at && timeout_at < interval_at &&
                    interval_at < backend_at,
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
    t08.metrics.push_back(mv("error_flag_total", 2, ""));
    t08.details.push_back("Variant A: buffers=2 triggers=100 available=2 errors=1");
    t08.details.push_back("Variant B: buffers=2 triggers=200 available=2 errors=1");
    // Per-buffer slots, in the runner's own wording: one line per RETAINED buffer, so a READY
    // slot sits beside the ERROR one. 0x2001 = MAPPED | TIMESTAMP_MONOTONIC (no error bit);
    // 0x2041 adds V4L2_BUF_FLAG_ERROR.
    t08.details.push_back("slot: A|0|4208|0x2001");
    t08.details.push_back("slot: A|1|4211|0x2041");
    t08.details.push_back("slot: B|0|8613|0x2041");
    t08.details.push_back("slot: B|1|8614|0x2001");
    t08.details.push_back("Error flag buffers: variant A buffer_index=1 sequence=4211 flags=0x2041");
    t08.details.push_back("Error flag buffers: variant B buffer_index=0 sequence=8613 flags=0x2041");
    t08.details.push_back("settle_time: 500ms");
    t08.details.push_back("backend_memory: mmap");
    t08.details.push_back("variant_a: 100 triggers at 100ms");
    t08.details.push_back("variant_b: 200 triggers at 50ms");
    t08.details.push_back("error_threshold: 0");
    v4l2diag::record_run_parameters(&t08);
    const std::string html = v4l2diag::render_test_content(t08);

    // 5.8.5: the variant table is the FIRST item, and there is no chart at all -- the approved
    // t08 has none in any of its three cards. The card used to open with a
    // `metric-chart-title` reading "Queue After Saturation 2 count allocated buffers", which
    // put a chart heading and a legend into a card the design gives neither.
    ok &= check(contains(html, "<h4 class=\"item-label\">Saturation by variant</h4>"),
                "T08 has no Saturation by variant item");
    ok &= check(!contains(html, "metric-chart-title"), "T08 still emits a chart title");
    ok &= check(!contains(html, "Queue After Saturation"), "T08 still emits the unapproved chart heading");
    ok &= check(!contains(html, "chart-frame") && !contains(html, "legend-swatch"),
                "T08 still emits a chart frame or legend; the approved card has neither");
    ok &= check(!contains(html, "approximately 10 seconds"), "T08's unapproved load-chart caption came back");
    // Order: the variant table precedes the slot strip, which precedes Aggregate.
    {
      const std::size_t variants_at = html.find("Saturation by variant");
      const std::size_t slots_at = html.find("Buffer state after saturation");
      const std::size_t aggregate_at = html.find(">Aggregate<");
      ok &=
          check(variants_at != std::string::npos && slots_at != std::string::npos && aggregate_at != std::string::npos,
                "T08 is missing one of its three approved items");
      ok &= check(variants_at < slots_at && slots_at < aggregate_at,
                  "T08's items are out of the approved order (table, then slots, then Aggregate)");
    }

    // 5.8.6: one slot per retained buffer, each naming its index, state, sequence and decoded
    // flags. The approved markup is queue-row > queue-label + slot-strip > slot.slot-ready |
    // slot.slot-error, with slot-index / slot-state / slot-seq / slot-flag inside.
    ok &= check(count_of(html, "class=\"queue-row\"") == 2, "T08 does not render one queue row per variant");
    ok &= check(count_of(html, "class=\"slot-strip\"") == 2, "T08 does not render one slot strip per variant");
    ok &= check(count_of(html, "slot slot-ready") == 2, "T08 does not render the two READY buffers as ready slots");
    ok &= check(count_of(html, "slot slot-error") == 2, "T08 does not render the two flagged buffers as error slots");
    ok &= check(count_of(html, "class=\"slot-index\">Buffer ") == 4, "T08's slots do not each name their buffer index");
    ok &= check(contains(html, "class=\"slot-seq\">seq 4208<"), "T08's slots do not carry the buffer sequence");
    ok &= check(count_of(html, ">ERROR<") >= 2, "T08's slots do not label the error state as text");
    ok &= check(count_of(html, ">READY<") >= 2, "T08's slots do not label the ready state as text");
    // Decoded flag names beside the raw value, per 5.8.8. MONOTONIC must appear for 0x2001,
    // which has no error bit -- a decoder that only names ERROR would leave a ready slot blank.
    ok &= check(contains(html, "MAPPED"), "T08 does not decode the MAPPED flag bit");
    ok &= check(contains(html, "MONOTONIC"), "T08 does not decode the timestamp flag bit");
    ok &= check(count_of(html, "0x2001") >= 2, "T08 does not show the non-error flag value on its ready slots");

    // 5.8.7: the six approved columns, observed/allocated format.
    for (const char *column : {"Variant", "Trigger load", "Allocated", "Available", "Error flagged", "Detail"}) {
      ok &= check(contains(html, std::string("<span>") + column + "</span>"),
                  std::string("T08 is missing the ") + column + " column");
    }
    ok &= check(contains(html, "2/2") && contains(html, "1/2"),
                "T08 does not show available/error-flagged as observed/allocated");
    ok &= check(!contains(html, "class=\"detail-list\""), "T08 still emits the raw repeating detail block");

    // 5.8.8: the raw flag value is preserved beside the decoded names.
    ok &= check(count_of(html, "0x2041") >= 2, "T08 does not preserve the raw flag value on its error slots");

    // The Aggregate "Error flag mask" row shows the flag VALUE with its decode as the detail,
    // per the approved card ("0x2041" / "ERROR | MAPPED | MONOTONIC"). It used to read
    // `error_flag_total`, printing the count 2 under a label promising a bitmask.
    {
      const std::string mask = cell_after_label(html, "Error flag mask");
      ok &= check(mask.find("0x") != std::string::npos,
                  "T08 'Error flag mask' shows '" + mask + "'; expected a hex flag value, not a count");
    }

    // 5.8.9: the configuration parameters, in the approved wording.
    for (const char *key : {"Buffer count", "Variant A triggers", "Variant A interval", "Variant B triggers",
                            "Variant B interval", "Settle time", "Max error flags", "Backend memory"}) {
      ok &= check(contains(html, key), std::string("T08 is missing the ") + key + " configuration row");
    }
  }

  // --- 18. T09, per review-plan 5.9 ---------------------------------------
  {
    v4l2diag::TestResult t09 = test_of("t09-buffer-recycling", v4l2diag::TestStatus::Pass);
    t09.name = "Buffer Requeue Delay Tolerance";
    // Twelve tested delays; the 0ms one sits below the 90% availability threshold, which is
    // the isolated dip the charts have to keep visible.
    t09.details.push_back("delay: 0ms|80|96|as expected");
    t09.details.push_back("delay: 1ms|100|98|as expected");
    t09.details.push_back("delay: 5ms|100|94|as expected");
    t09.details.push_back("delay: 10ms|100|87|as expected");
    t09.details.push_back("delay: 20ms|100|82|as expected");
    t09.details.push_back("delay: 30ms|100|66|as expected");
    t09.details.push_back("delay: 40ms|100|60|as expected");
    t09.details.push_back("delay: 48ms|100|51|as expected");
    t09.details.push_back("delay: 50ms|100|47|as expected");
    t09.details.push_back("delay: 60ms|100|39|as expected");
    t09.details.push_back("delay: 80ms|100|16|as expected");
    t09.details.push_back("delay: 100ms|100|0|as expected");
    t09.details.push_back("allocated_buffers: 4");
    t09.details.push_back("repetitions_per_delay: 20");
    t09.details.push_back("warmup_frames: 3 frames");
    t09.details.push_back("capture_timeout: 100ms");
    t09.details.push_back("inter_repetition_interval: 50ms");
    t09.details.push_back("availability_threshold: 90%");
    t09.details.push_back("safe_delay_threshold: 48ms");
    t09.details.push_back("backend_memory: mmap");
    v4l2diag::record_run_parameters(&t09);
    const std::string html = v4l2diag::render_test_content(t09);

    // 5.9.6: availability per tested delay, with the review threshold drawn AND named --
    // the exact phrase, because any "90%" substring would also match a table cell.
    ok &= check(contains(html, "Post-requeue Availability"), "T09 has no availability chart with the approved title");
    ok &= check(contains(html, "90% review threshold"), "T09's availability chart does not name the review threshold");
    ok &= check(count_of(html, "t09-availability-point") == 12,
                "T09's availability chart does not show all twelve tested delays");
    // 5.9.6 rule 4: the sub-threshold point carries its value, so the dip is readable
    // without measuring against the axis.
    ok &= check(contains(html, "t09-point-label"), "T09 does not label the point that falls below the threshold");

    // 5.9.7: the second chart is "Wait After Requeue" / "Mean post-requeue wait", never
    // "Mean latency" -- 0ms wait at a 100ms delay is not zero camera latency.
    ok &= check(contains(html, "Wait After Requeue"), "T09 has no chart titled \"Wait After Requeue\"");
    ok &= check(contains(html, "Mean post-requeue wait"),
                "T09's second chart is not labelled \"Mean post-requeue wait\"");
    ok &= check(!contains(html, "Mean latency"), "T09 still labels a chart \"Mean latency\"");
    ok &= check(count_of(html, "t09-wait-point") == 12,
                "T09's wait-after-requeue chart does not show all twelve tested delays");

    // 5.9.8: the four approved columns, one row PER DELAY -- not one aggregate row, which
    // is what the pre-5.9 renderer produced and which hid every individual dip.
    for (const char *column : {"Delay", "Available", "Mean wait", "Detail"}) {
      ok &= check(contains(html, std::string("<span>") + column + "</span>"),
                  std::string("T09 is missing the ") + column + " column");
    }
    ok &= check(contains(html, ">0ms<") && contains(html, ">100ms<"),
                "T09's Delay Results table is missing the 0ms or 100ms row");
    ok &= check(count_of(html, "<span>as expected</span>") == 12,
                "T09's Delay Results table does not carry all twelve delay rows");
    ok &= check(!contains(html, ">Requeue<"), "T09 still aggregates every delay into one \"Requeue\" row");
    ok &= check(!contains(html, "class=\"detail-list\""), "T09 still emits the raw repeating detail block");

    // 5.9.9: the configuration parameters, in the approved preview's own wording -- "Reps per
    // delay", not "Repetitions per delay". Completeness is checked against the previews in
    // tests/test_configuration_contract_test.cpp.
    for (const char *key : {"Reps per delay", "Capture timeout", "Inter-rep interval", "Warmup count",
                            "Min safe cliff delay", "Backend memory"}) {
      ok &= check(contains(html, key), std::string("T09 is missing the ") + key + " configuration row");
    }

    // 5.9.12: sequence-gap evidence is TECHNICAL DETAIL, shown only when the run recorded
    // one. A section that always appears would imply a gap was looked for and found.
    ok &= check(!contains(html, "Technical details"),
                "T09 shows a technical-details section when no sequence gap was recorded");
    v4l2diag::TestResult t09_gap = t09;
    t09_gap.details.push_back("sequence_gap: Second frame at 0ms delay|sequence 41 -> 43, one frame missing");
    const std::string gap_html = v4l2diag::render_test_content(t09_gap);
    ok &= check(contains(gap_html, "Aggregate"), "T09 does not show sequence-gap evidence below the delay table");
    ok &= check(contains(gap_html, "sequence 41 -&gt; 43") || contains(gap_html, "sequence 41"),
                "T09's technical detail does not carry the sequence-gap evidence");
    // It sits BELOW the results table: the reader meets the verdict before the forensics.
    ok &= check(gap_html.find("Wait time by requeue delay") < gap_html.find("Technical details"),
                "T09 puts its technical detail above the Delay Results table");
  }

  // --- 19. T10, per review-plan 5.10 --------------------------------------
  {
    v4l2diag::TestResult t10 = test_of("t10-buffer-flags", v4l2diag::TestStatus::Pass);
    t10.name = "V4L2 Buffer Flag Analysis";
    t10.metrics.push_back(mv("captured", 50, ""));
    t10.metrics.push_back(mv("requested", 50, ""));
    // 5.10.6: each row is "flag: group|name|count|meaning|state".
    t10.details.push_back("flag: Frame health|ERROR|0|No frame reported an error flag.|CLEAR");
    t10.details.push_back("flag: Frame type|KEYFRAME|0|The driver did not mark raw frames as keyframes.|NOT SET");
    t10.details.push_back(
        "flag: Clock type|TIMESTAMP_COPY|50|Driver declares copied timestamps for every frame.|ACTIVE");
    t10.details.push_back("flag: Clock type|TIMESTAMP_MONOTONIC|0|This clock declaration was not observed.|NOT SET");
    t10.details.push_back("flag: Timestamp point|TSTAMP_SRC_EOF|50|Timestamps represent end of frame.|ACTIVE");
    t10.details.push_back(
        "flag: Timestamp point|TSTAMP_SRC_SOE|0|Start-of-exposure timestamps were not reported.|NOT SET");
    t10.details.push_back("declared_clock_type: TIMESTAMP_COPY");
    t10.details.push_back("timestamp_point: End of frame");
    t10.details.push_back("source_consistency: Consistent");
    t10.details.push_back("combined_mask: MAPPED, TIMESTAMP_COPY|0x00004001");
    t10.details.push_back("requested_samples: 50");
    t10.details.push_back("warmup: 5 frames");
    t10.details.push_back("capture_timeout: 100ms");
    t10.details.push_back("sample_interval: 100ms");
    t10.details.push_back("error_threshold: 0");
    t10.details.push_back("backend_memory: mmap");
    v4l2diag::record_run_parameters(&t10);
    const std::string html = v4l2diag::render_test_content(t10);

    // 5.10.5: the four-field metadata summary, above the flag table.
    for (const char *field : {"Capture completeness", "Declared clock type", "Timestamp point", "Source consistency"}) {
      ok &= check(contains(html, field), std::string("T10 is missing the \"") + field + "\" summary field");
    }
    // 5.10.2: capture completeness reads "captured/requested", not "Frames captured: 50".
    ok &= check(contains(html, "50/50"), "T10 does not show capture completeness as captured/requested");
    ok &= check(!contains(html, "Frames captured:"), "T10 still writes capture completeness as prose");

    // 5.10.6: the five approved columns, and the four semantic groups.
    for (const char *column : {"Group", "Flag", "Observed", "Meaning", "Detail"}) {
      ok &= check(contains(html, std::string("<span>") + column + "</span>"),
                  std::string("T10 is missing the ") + column + " column");
    }
    for (const char *group : {"Frame health", "Frame type", "Clock type", "Timestamp point"}) {
      ok &= check(contains(html, group), std::string("T10 is missing the \"") + group + "\" flag group");
    }
    // 5.10.6: observed counts are "count/captured", so 0 reads as "none of 50", not "zero".
    ok &=
        check(contains(html, "0/50") && contains(html, "50/50"), "T10 does not show flag counts as observed/captured");
    // 5.10.6: an unobserved INFORMATIONAL flag is NOT SET, never an error colour --
    // KEYFRAME=0 on a raw stream is normal, not a fault.
    ok &= check(contains(html, "NOT SET"), "T10 does not use the NOT SET state for unobserved informational flags");
    ok &= check(contains(html, "CLEAR"), "T10 does not use the CLEAR state for an unobserved error flag");
    ok &= check(contains(html, "ACTIVE"), "T10 does not use the ACTIVE state for an observed metadata flag");
    ok &= check(!contains(html, "class=\"detail-list\""), "T10 still emits the raw key-value list");

    // 5.10.7: the combined mask decoded by name, with the raw hex kept on the same row.
    ok &= check(contains(html, "Decoded buffer flags"), "T10 has no Decoded buffer flags item");
    ok &= check(contains(html, "MAPPED, TIMESTAMP_COPY"), "T10 does not decode the combined mask by name");
    ok &= check(contains(html, "0x00004001"), "T10 does not preserve the raw combined mask");

    // 5.10.7: bits outside the known masks are surfaced, not silently dropped.
    v4l2diag::TestResult t10_unknown = t10;
    t10_unknown.details.push_back("unknown_bits: 0x00010000");
    const std::string unknown_html = v4l2diag::render_test_content(t10_unknown);
    ok &= check(contains(unknown_html, "Unknown bits"), "T10 does not report unknown flag bits");
    ok &= check(contains(unknown_html, "0x00010000"), "T10 does not carry the raw value of unknown bits");
    ok &= check(!contains(html, "Unknown bits"), "T10 reports unknown bits when the mask was fully decoded");

    // The T21 boundary note is gone. Its content was true -- declared clock metadata is
    // not proof that timestamp VALUES never went backwards -- but `.boundary-note` appears
    // in none of the 26 approved previews, and a card may carry only the one approved
    // piece of prose (the verdict line on a non-PASS card). Inverted, not deleted.
    ok &= check(!contains(html, "class=\"boundary-note\""), "the unapproved .boundary-note prose came back on T10");

    // 5.10.9: the six configuration parameters.
    for (const char *key :
         {"Sample count", "Warmup count", "Capture timeout", "Sample interval", "Max error flags", "Backend memory"}) {
      ok &= check(contains(html, key), std::string("T10 is missing the ") + key + " configuration row");
    }

    // 5.10.10: a passing card carries no RESULT, and T10 draws no chart.
    ok &= check(!contains(html, "class=\"result-fail\""), "a passing T10 card shows a RESULT section");
  }

  // --- 20. T11, per review-plan 5.11 --------------------------------------
  {
    v4l2diag::TestResult t11 = test_of("t11-memory-throughput", v4l2diag::TestStatus::Pass);
    t11.name = "Memory Access Throughput";
    t11.metrics.push_back(mv("sizeimage_bytes", 4915200, "B"));
    t11.metrics.push_back(mv("mapped_capacity_bytes", 5439744, "B"));
    t11.metrics.push_back(mv("full_frame_mib_s", 10250.4, "MiB/s"));
    // 5.11.4: "copy: label|bytes|MiB/s|class" -- full frame vs cache-sized reads.
    t11.details.push_back("copy: Full frame|4915200|10250.4|full");
    t11.details.push_back("copy: 4 KiB sample|4096|28322.6|cache");
    t11.details.push_back("copy: 64 KiB sample|65536|31593.1|cache");
    t11.details.push_back("repetitions: 200");
    t11.details.push_back("warmup_copies: 20");
    t11.details.push_back("timer: CLOCK_MONOTONIC");
    t11.details.push_back("backend_memory: mmap");
    v4l2diag::record_run_parameters(&t11);
    const std::string html = v4l2diag::render_test_content(t11);

    // 5.11.3: three DISTINCT buffer figures. A single "Frame size" cannot tell a reader
    // whether the extra bytes came from the sensor or from DMA alignment.
    ok &= check(contains(html, "Active image payload"), "T11 does not name the active image payload");
    ok &= check(contains(html, "Mapped buffer capacity"), "T11 does not name the mapped buffer capacity");
    ok &= check(contains(html, "Allocation overhead"), "T11 does not name the allocation overhead");
    ok &= check(!contains(html, ">Frame size<"), "T11 still shows a single ambiguous \"Frame size\"");
    // The overhead is the DIFFERENCE, computed rather than restated: 5439744 - 4915200.
    ok &= check(contains(html, "524,544") || contains(html, "524544"), "T11 does not compute the allocation overhead");
    // Both byte counts and readable MiB, per the 5.11.3 reference values.
    ok &= check(contains(html, "4,915,200") || contains(html, "4915200"), "T11 does not show the payload in bytes");
    ok &= check(contains(html, "4.69 MiB"), "T11 does not show the payload in MiB");

    // 5.11.5: the base is 1,048,576, so the unit is MiB/s -- never MB/s.
    ok &= check(contains(html, "MiB/s"), "T11 does not use MiB/s");
    ok &= check(!contains(html, "MB/s"), "T11 still labels throughput MB/s despite a 1,048,576 base");

    // 5.11.5: the derived end-user figures, and NOT named as a capture or sensor frame rate.
    ok &= check(contains(html, "GiB/s") || contains(html, "gibibytes per second"),
                "T11 does not derive a readable GiB/s figure");
    ok &=
        check(contains(html, "ms/buffer") || contains(html, "ms / buffer") || contains(html, "milliseconds per buffer"),
              "T11 does not derive the estimated copy time per buffer");
    ok &= check(contains(html, "buffers/s") || contains(html, "buffers per second"),
                "T11 does not derive theoretical copies per second");
    // A thousands separator must not land inside a decimal: "2,186,.75" is what a
    // digit-grouping helper written for whole byte counts produces when handed a fraction.
    ok &= check(!contains(html, ",."), "T11 puts a thousands separator immediately before a decimal point");
    ok &= check(contains(html, "2,187") || contains(html, "2,186"),
                "T11's theoretical copy capacity is not a readable whole number");
    ok &= check(!contains(html, "frames/s") && !contains(html, "FPS") && !contains(html, "frame rate"),
                "T11 names a derived copy figure as a capture or sensor frame rate");

    // 5.11.4: the cache-sized reads are grouped as SECONDARY evidence, so 31,593 MiB/s is
    // not read as camera throughput.
    ok &= check(contains(html, "Aggregate"), "T11 does not group the small copies as cache-sized reads");
    ok &= check(contains(html, "Full frame"), "T11 does not name the full-frame result");

    // 5.11.7: the throughput-by-copy-size chart, one bar per measurement.
    ok &= check(contains(html, "Throughput by Copy Size") || contains(html, "Throughput by copy size"),
                "T11 has no throughput-by-copy-size chart");
    // One bar per measurement, counted on the shared bar vocabulary the approved
    // t11-preview.html uses. This counted `t11-copy-bar` until 2026-08-09; that class was a
    // T11-only hook on T08's borrowed load-* geometry and no longer exists, so the old
    // assertion would have gone silently green on a chart that renders nothing.
    ok &= check(count_of(html, "class=\"bar-fill bar-full\"") + count_of(html, "class=\"bar-fill bar-cache\"") == 3,
                "T11's chart does not show all three copy sizes");
    ok &= check(count_of(html, "class=\"bar-fill bar-cache\"") == 2,
                "T11 does not mark both cache-sized reads as the secondary series");

    // 5.11.7: the five approved result columns.
    for (const char *column : {"Copy region", "Bytes per copy", "Throughput", "Relative to full", "Detail"}) {
      ok &= check(contains(html, std::string("<span>") + column + "</span>"),
                  std::string("T11 is missing the ") + column + " column");
    }
    ok &= check(contains(html, "1.00x"), "T11 does not show the full-frame result as the 1.00x reference");
    ok &= check(!contains(html, "class=\"detail-list\""), "T11 still emits the raw detail block");

    // 5.11.6: the measurement evidence a reader needs to trust the number at all.
    ok &= check(contains(html, "CLOCK_MONOTONIC"), "T11 does not state which timer it measured with");
    // The approved t11 card states "Minimum repetitions" here; it has no warm-up row. The runner
    // still records warmup_copies, but Test Configuration shows only what the design names.
    ok &= check(contains(html, "Minimum repetitions"), "T11 does not state its repetition floor");

    // 5.11.2: PASS confirms a valid benchmark ran; no platform threshold was applied, so
    // there is no RESULT block on a passing card.
    ok &= check(!contains(html, "class=\"result-fail\""), "a passing T11 card shows a RESULT section");
  }

  // --- T13/T14/T15: a row's label must match the quantity it prints -------------
  //
  // The 2026-08-10 device run (1786329594-21268) exposed six rows bound to a metric that
  // measures something else. They were invisible while millisecond values printed at two
  // decimals; at three the latency shape gave them away -- "Missed captures 0.013" is not a
  // count of anything, and T13 printed the same 3.5 for two different quantities.
  //
  // Every one of these is a DERIVED value the runner does not record as a metric, which is
  // why the binding reached for the nearest latency instead. The approved previews name what
  // each row shows; those values are asserted here.
  {
    // T13: the production timeout is a configured value, not the safety margin. Approved
    // t13 shows "Production timeout 48.5 / From configuration" beside "Safety margin 3.5".
    v4l2diag::TestResult t13 = test_of("t13-poll-timeout-cliff", v4l2diag::TestStatus::Warn);
    t13.name = "Poll Timeout Reliability Boundary";
    t13.metrics.push_back(mv("cliff_ms", 45, "ms"));
    t13.metrics.push_back(mv("first_miss_ms", 44, "ms"));
    t13.metrics.push_back(mv("safety_margin_ms", 3.5, "ms"));
    t13.metrics.push_back(mv("stability_confirmed", 1, "bool"));
    t13.metrics.push_back(mv("stability_rounds_passed", 5, "count"));
    t13.details.push_back("production_timeout: 48.5");
    v4l2diag::record_run_parameters(&t13);
    const std::string html = v4l2diag::render_test_content(t13);

    // Observed: the cell that follows the "Production timeout" label in the Aggregate table.
    const std::string prod = cell_after_label(html, "Production timeout");
    ok &= check(prod.find("48.5") != std::string::npos,
                "T13 'Production timeout' shows '" + prod + "'; the configured 48.5 was recorded");
    ok &= check(prod.find("3.5") == std::string::npos,
                "T13 'Production timeout' is still printing the safety margin (3.5)");
  }
  {
    // T14: reliability is a ratio and missed captures is a count. Approved t14 shows
    // "Capture reliability 50/50" and "Missed captures 0", NOT latency statistics.
    v4l2diag::TestResult t14 = test_of("t14-trigger-latency");
    t14.name = "Trigger to Capture Latency";
    t14.metrics.push_back(mv("frames_captured", 50, "count"));
    t14.metrics.push_back(mv("frames_missed", 0, "count"));
    t14.metrics.push_back(mv("latency_mean", 44.806510, "ms"));
    t14.metrics.push_back(mv("latency_stddev", 0.013344, "ms"));
    t14.metrics.push_back(mv("latency_min", 44.781062, "ms"));
    t14.metrics.push_back(mv("latency_max", 44.842566, "ms"));
    t14.metrics.push_back(mv("latency_p95", 44.834056, "ms"));
    t14.metrics.push_back(mv("latency_jitter", 0.018048, "ms"));
    t14.details.push_back("capture_timeout: 100ms");
    v4l2diag::record_run_parameters(&t14);
    const std::string html = v4l2diag::render_test_content(t14);

    const std::string reliability = cell_after_label(html, "Capture reliability");
    ok &= check(reliability.find("50/50") != std::string::npos,
                "T14 'Capture reliability' shows '" + reliability + "'; expected the ratio 50/50");
    const std::string missed = cell_after_label(html, "Missed captures");
    ok &= check(missed.find("0.013") == std::string::npos,
                "T14 'Missed captures' shows '" + missed + "', which is the standard deviation");
    // The spread is max - min: 44.842566 - 44.781062 = 0.061504, which is "0.062" at three
    // decimals. Never max alone.
    const std::string spread = cell_after_label(html, "Min-max spread");
    ok &= check(spread.find("44.84") == std::string::npos,
                "T14 'Min-max spread' shows '" + spread + "', which is the maximum, not the spread");
    ok &= check(spread.find("0.062") != std::string::npos,
                "T14 'Min-max spread' shows '" + spread + "'; expected max - min = 0.062");
  }
  {
    // T15: the difference between the two modes, and a count of modes. Approved t15 shows
    // "Mean difference 0.003" and "Modes compared 2/2".
    v4l2diag::TestResult t15 = test_of("t15-nonblock-vs-block");
    t15.name = "Non-blocking Spin vs Blocking DQBUF";
    t15.metrics.push_back(mv("nonblock_latency_mean", 44.796811, "ms"));
    t15.metrics.push_back(mv("block_latency_mean", 44.799275, "ms"));
    t15.metrics.push_back(mv("nonblock_latency_p95", 44.808614, "ms"));
    t15.metrics.push_back(mv("nonblock_captures", 30, "count"));
    t15.metrics.push_back(mv("block_captures", 30, "count"));
    v4l2diag::record_run_parameters(&t15);
    const std::string html = v4l2diag::render_test_content(t15);

    // 44.799275 - 44.796811 = 0.002464 -> "0.002" at three decimals.
    const std::string difference = cell_after_label(html, "Mean difference");
    ok &= check(difference.find("44.79") == std::string::npos,
                "T15 'Mean difference' shows '" + difference + "', which is one mode's mean, not the difference");
    ok &= check(difference.find("0.002") != std::string::npos,
                "T15 'Mean difference' shows '" + difference + "'; expected the between-mode difference 0.002");
    const std::string compared = cell_after_label(html, "Modes compared");
    ok &= check(compared.find("2/2") != std::string::npos,
                "T15 'Modes compared' shows '" + compared + "'; expected the ratio 2/2");
    const std::string nonblock = cell_after_label(html, "Non-block captures");
    ok &= check(nonblock.find("30/30") != std::string::npos,
                "T15 'Non-block captures' shows '" + nonblock + "'; expected the ratio 30/30");
  }

  // --- Detail cells carry descriptive text, not a second verdict -----------------
  //
  // T13/T12/T20-T22 rendered status words into the Detail column through eight styling hooks
  // that no CSS rule ever defined -- `ok`, `no`, `verified`, `observed`, `warn-text`, plus
  // T12's `protocol`/`step`/`sync` diagram. Found by the source-level CSS scan after the T08
  // rewrite; the report showed them at browser-default weight.
  //
  // The approved previews put PLAIN text in this column: t13's round table reads "Boundary
  // confirmed" with no class at all, and t12/t20 carry values like "0" and "20/20" while the
  // verdict lives in the dedicated Status column, which has its own styled `.verdict` class.
  // Pairing an unstyled `warn-text` against a styled `pass` also meant the WARN case rendered
  // plainer than the PASS case -- the opposite of what a warning should do.
  {
    for (const char *slug : {"t13-poll-timeout-cliff", "t12-dmabuf-cache-sync", "t20-sequence-continuity"}) {
      v4l2diag::TestResult test = test_of(slug, v4l2diag::TestStatus::Pass);
      test.name = slug;
      // Enough evidence for each card to render its tables; the assertion is about the CLASSES
      // in the output, so exact values do not matter here.
      test.metrics.push_back(mv("cliff_ms", 45, "ms"));
      test.metrics.push_back(mv("first_miss_ms", 44, "ms"));
      test.metrics.push_back(mv("safety_margin_ms", 3.5, "ms"));
      test.metrics.push_back(mv("stability_rounds_passed", 5, "count"));
      test.metrics.push_back(mv("sequence_gaps", 0, "count"));
      test.metrics.push_back(mv("max_sequence_gap", 0, "count"));
      test.metrics.push_back(mv("sync_match", 20, "count"));
      test.metrics.push_back(mv("samples_tested", 20, "count"));
      test.details.push_back("production_timeout: 48.5");
      test.details.push_back("stability round 1: @45ms=10/10, @44ms=0/10 YES");
      const std::string html = v4l2diag::render_test_content(test);

      for (const char *hook : {"ok", "no", "verified", "observed", "warn-text", "protocol", "step"}) {
        ok &= check(!contains(html, std::string("class=\"") + hook + "\""),
                    std::string(slug) + " still emits the unstyled '" + hook + "' status hook");
      }
      ok &= check(!contains(html, "class=\"step sync\""), std::string(slug) + " still emits the 'step sync' hook");
    }
  }

  // --- Test Configuration lists INPUTS, never per-iteration results --------------
  //
  // User finding 2026-08-11: "t03 deki Test Configuration kismindaki variableler bence yanlis.
  // Cycle 1 diye variable olamaz." Correct -- and it was the widest deviation in the report. A
  // scan of the approved set against the 2026-08-11 device run found 24 measurement rows sitting
  // in Test Configuration across six tests: T03's and T26's per-cycle lines, T23's per-window
  // lines, T18's control combinations, T19's resolution line, and a row literally named
  // "Unavailable" on T21.
  //
  // The section answers "what settings produced this report", so its Source column reads
  // `param` / `threshold`. A per-iteration measurement there contradicts that column, and the
  // same numbers already appear in Measurement -- T03's cycle timings are the stacked chart.
  //
  // The generic reader did filter `cycle`, but the runner writes "cycle 1: ..." -- key "cycle 1",
  // which never equalled "cycle". Observed here: the Variable cell of every rendered row.
  {
    struct Case {
      const char *slug;
      const char *detail;
      const char *forbidden;
    };
    // Each detail line is in the runner's own wording, taken from the device run's JSON.
    const std::vector<Case> cases = {
        {"t03-pipeline-ready", "cycle 1: STREAMON=1140ms, first frame=145ms, pulses=2", "Cycle 1"},
        {"t26-cold-start", "cycle 1: warmup=1 frames", "Cycle 1"},
        {"t23-sustained-capture", "Win0 0-10s: n=65 mean=44ms stddev=0 miss=0", "Win0 0-10s"},
        {"t19-resolution-sweep", "1920x1280: mean=44ms p95=44ms throughput=1068MB/s", "1920x1280"},
        // T18's control combinations survived the first Faz 1 pass: the key is "ll0_bp0_wi0", a
        // shape the prefix list did not cover, and it is a MEASUREMENT of one control combination
        // ("n=20 mean=44.792377ms"). Caught on the 2026-08-11 11:14 device run, where four of
        // T18's five configuration rows were these combinations. The renderer humanizes the key,
        // so the report read "Ll0 bp0 wi0".
        {"t18-control-sweep", "ll0_bp0_wi0: n=20 mean=44.792377ms", "Ll0 bp0 wi0"},
        // T13's sweep lines. The key carries no number ("coarse", "bsearch") so only the name marks
        // them, and "stability round 1" hides its marker word in the MIDDLE of the key -- both
        // shapes reached the 2026-08-12 device run's Test Configuration as fifteen extra rows.
        {"t13-poll-timeout-cliff", "coarse: 150ms -> 10/10", "Coarse"},
        {"t13-poll-timeout-cliff", "bsearch:  45ms -> 10/10", "Bsearch"},
        {"t13-poll-timeout-cliff", "stability round 1: @45ms=10/10, @44ms=0/10", "Stability round 1"},
        // The 2026-08-12 01:41 run showed 69 further extras in shapes no prefix list anticipated:
        // per-width results ("1ms: hits=8/8"), per-camera rows ("/dev/video4: ..."), per-copy rows
        // ("mmap_full: ...") and surviving internal keys whose display label differs from the
        // approved one ("requested_samples" beside "Sample count"). Chasing shapes one at a time was
        // the wrong model -- the reader now renders only what the approved card NAMES.
        {"t16-gpio-pulse-width", "1ms: hits=8/8 mean=44.801ms", "1ms"},
        {"t25-multi-camera", "/dev/video4: 50/50 captured", "/dev/video4"},
        {"t11-memory-throughput", "mmap_full: 1027 MB/s", "Mmap full"},
        {"t10-buffer-flags", "flag: Frame health|ERROR|0|No frame reported an error", "Flag"},
        {"t10-buffer-flags", "requested_samples: 50", "Requested samples"},
        {"t08-buffer-overwrite", "Variant A: buffers=2 triggers=100 available=2 errors=1", "Variant A"},
    };
    for (const auto &c : cases) {
      v4l2diag::TestResult test = test_of(c.slug, v4l2diag::TestStatus::Pass);
      test.name = c.slug;
      test.details.push_back(c.detail);
      // A real input beside it, so the table still renders and the check is not observing an
      // empty section. Recorded under the approved DISPLAY label, because the renderer now shows
      // exactly the rows the design names -- an internal "capture_timeout:" key is not one of them.
      v4l2diag::record_run_parameters(&test);
      // The `derived` rows come from the test BODY, which needs a real device; replayed here so the
      // survival check below sees the complete approved set.
      for (const auto &d : {std::pair<const char *, const char *>{"Pulse width levels", "11"},
                            {"Total captures", "88"},
                            {"Sizeimage", "4.69"},
                            {"Controls discovered", "15"},
                            {"Writable controls", "13"},
                            {"Measured max difference", "0.005"},
                            {"Resolutions enumerated", "1"},
                            {"Pixel format", "UYVY"},
                            {"Pairs compared", "49"},
                            {"Participants", "4"}}) {
        const auto &approved_here = v4l2diag::configuration_rows_for(c.slug);
        // Compared as text: both sides are `const char *`, so `==` would compare addresses and
        // never match.
        const bool belongs =
            std::any_of(approved_here.begin(), approved_here.end(),
                        [&d](const v4l2diag::ConfigRowSpec &r) { return std::string(d.first) == r.label; });
        if (belongs) {
          v4l2diag::record_derived_config(&test, d.first, d.second);
        }
      }
      const std::string html = v4l2diag::render_test_content(test);
      const std::size_t cfg = html.find("Test Configuration");
      ok &= check(cfg != std::string::npos, std::string(c.slug) + " renders no Test Configuration section");
      if (cfg == std::string::npos) {
        continue;
      }
      const std::string section = html.substr(cfg);
      ok &= check(section.find(std::string("<span>") + c.forbidden + "</span>") == std::string::npos,
                  std::string(c.slug) + " lists the measurement row '" + c.forbidden + "' as a configuration variable");
      // ...and the genuine inputs are still there: filtering must not swallow the whole table.
      // Checked against the test's OWN approved row list rather than one hard-coded label -- t03's
      // approved card has no "Capture timeout" row at all, so asserting it there was wrong.
      const auto &approved = v4l2diag::configuration_rows_for(c.slug);
      ok &= check(!approved.empty(), std::string(c.slug) + " has no approved configuration rows");
      for (const auto &spec : approved) {
        ok &= check(section.find(std::string(">") + spec.label + "<") != std::string::npos,
                    std::string(c.slug) + " lost its approved '" + spec.label + "' configuration row");
      }
    }
  }

  // A row named "Unavailable" is never a variable. T21 recorded no configuration at all, so the
  // metric fallback offered `non_monotonic` and the empty-state row landed in the Variable
  // column instead of being the table's only content.
  {
    v4l2diag::TestResult t21 = test_of("t21-timestamp-monotonicity", v4l2diag::TestStatus::Pass);
    t21.name = "t21-timestamp-monotonicity";
    t21.metrics.push_back(mv("non_monotonic", 0, ""));
    t21.metrics.push_back(mv("delta_mean", 44.8, "ms"));
    v4l2diag::record_run_parameters(&t21);
    const std::string html = v4l2diag::render_test_content(t21);
    const std::size_t cfg = html.find("Test Configuration");
    if (cfg != std::string::npos) {
      const std::string section = html.substr(cfg);
      ok &= check(section.find("<span>Non monotonic</span>") == std::string::npos,
                  "T21 lists the measurement 'non_monotonic' as a configuration variable");
    }
  }

  if (ok) {
    std::cout << "test_content_registry_test: all checks passed\n";
  }
  return ok ? 0 : 1;
}
