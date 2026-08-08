// Locks the S1-S10 card contract from docs/report-ui-design-spec.md on the
// HTML that write_reports() actually produces.
//
// The 26 approved previews in docs/assets/refactored_previews/ are the target;
// this test states what the renderer must emit to match them. Each assertion
// names the observed thing (a tag, a column header, a CSS class) rather than a
// test title, so a green run means the markup was inspected, not that a
// function was called.

#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "v4l2diag/core/report_naming.hpp"
#include "v4l2diag/core/report_writer.hpp"
#include "v4l2diag/core/types.hpp"

namespace {

int failures = 0;

bool check(bool condition, const std::string &message) {
  if (!condition) {
    std::cout << "FAIL: " << message << "\n";
    ++failures;
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

std::string make_temp_dir() {
  char pattern[] = "/tmp/v4l2diag-card-XXXXXX";
  return mkdtemp(pattern);
}

std::string read_file(const std::string &path) {
  std::ifstream input(path);
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

v4l2diag::ReportNaming naming_of(const v4l2diag::RunResult &run) {
  v4l2diag::ReportNaming naming;
  naming.started_at_utc = run.started_at_utc;
  naming.trigger_mode = run.trigger_mode;
  naming.trigger_profile_file = run.trigger_profile_file;
  naming.test_configuration_file = run.threshold_config_file;
  return naming;
}

v4l2diag::TestResult test_of(const std::string &id, const std::string &name, v4l2diag::TestStatus status) {
  v4l2diag::TestResult test;
  test.id = id;
  test.name = name;
  test.status = status;
  test.memory_backend = "mmap";
  test.duration_ms = 1500;
  test.summary = name + " completed.";
  test.category = "capture";
  // Every metric name any verdict row reads, so a row that renders "Unavailable" here
  // means the SPEC names something the runner never records -- not that this fixture
  // happens to be thin. Values are arbitrary; only the names are load-bearing.
  static const char *kNumeric[] = {
      "frames", "frames_captured", "frames_missed", "captured", "requested", "pollerr_events", "full_cycles",
      "full_configured", "full_start_fail", "rapid_cycles", "rapid_configured", "rapid_start_fail", "allocated_buffers",
      "mismatches", "sync_max_ms", "cliff_ms", "safety_margin_ms", "stability", "latency_mean_ms", "latency_max_ms",
      "latency_stddev_ms",
      // The names the RUNNER emits (push_stats_metrics
      // writes "<prefix>_<stat>", with the unit in its own
      // field). The fixture carried the renderer's guesses
      // instead, which hid the mismatch from this test.
      "latency_mean", "latency_p95", "latency_max", "latency_min", "latency_stddev", "latency_jitter",
      "nonblock_latency_mean", "nonblock_latency_p95", "nonblock_latency_max", "nonblock_latency_min",
      "nonblock_latency_stddev", "block_latency_mean", "block_latency_p95", "block_latency_max", "block_latency_min",
      "block_latency_stddev", "baseline_latency_mean", "full_cycles_success", "full_cycles_attempted",
      "full_cycle_failures", "rapid_cycles_ok", "rapid_cycles_attempted", "rapid_start_failures",
      "first_frame_timeouts", "first_frame_latency_mean", "first_frame_latency_max", "streamon_ms_mean",
      "streamon_ms_max", "avg_eagain_spins", "non_monotonic", "delta_mean", "delta_max", "warmup_mean_frames",
      "warmup_max_frames", "writable_count", "frame_interval_ms", "spread_h", "spread_l", "trigger_fire_spread_mean",
      "trigger_fire_spread_max", "stability_confirmed", "supports_capture", "selected_backend_supported",
      "baseline_latency_p95", "load_latency_mean", "load_latency_p95", "nonblock_mean_ms", "block_mean_ms",
      "nonblock_p95_ms", "block_p95_ms", "hits_5", "hits_13", "hits_20", "uyvy_coverage", "yuyv_coverage",
      "uyvy_mean_ms", "yuyv_mean_ms", "captures", "ll0_bp1_wi0_mean_ms", "ll1_bp0_wi0_mean_ms", "ll1_bp1_wi1_mean_ms",
      "res_1920x1080_mean_ms", "measured", "enumerated", "gaps", "max_gap", "regressions", "stuck",
      "interval_jitter_ms", "load_p95_ms", "idle_p95_ms", "cameras", "cycles", "sessions", "full_frame_mib_s",
      "sizeimage_bytes", "mapped_capacity_bytes", "streamon_mean_ms", "streamon_max_ms", "first_frame_mean_ms",
      "first_frame_max_ms", "cycles_completed", "timeouts", "recovery_frames", "baseline_frames"};
  double seed = 12.0;
  for (const char *name : kNumeric) {
    v4l2diag::MetricValue metric;
    metric.name = name;
    metric.value = seed;
    test.metrics.push_back(metric);
    seed += 3.0;
  }
  // The structured detail lines the other verdict rows count or read.
  test.details.push_back("check: poll() before STREAMON|No readiness|Timed out|PASS");
  test.details.push_back("phase: Baseline capture|30/30 captured|30/30 captured|as expected");
  test.details.push_back("request: 1|2|20|20|59");
  test.details.push_back("variant: Variant A|100 at 10/s|2|2|2|1|2|WARN");
  test.details.push_back("evidence: Variant A|Buffer 0, sequence 0: ERROR|0x2041");
  test.details.push_back("delay: 0ms|80|96|as expected");
  test.details.push_back("flag: Frame health|ERROR|0|No frame reported an error");
  test.details.push_back("full_phase: Completed");
  test.details.push_back("rapid_phase: Completed");
  test.details.push_back("slow_start_guard: Not reached");
  test.details.push_back("availability_threshold: 90%");
  test.details.push_back("safe_delay_threshold: 48ms");
  test.details.push_back("capture_timeout: 100ms");
  test.details.push_back("samples_per_request: 20");
  test.details.push_back("copy: Full frame|4915200|10250.4|full");
  test.details.push_back("copy: 4 KiB|4096|18400.2|cache");
  return test;
}

// The card body only -- the surrounding page has its own contract.
std::string cards_of(const std::string &html) {
  const std::size_t at = html.find("<article class=\"test-card");
  return at == std::string::npos ? std::string() : html.substr(at);
}

// Every value of class="..." inside the cards, split into individual names.
std::set<std::string> card_classes(const std::string &cards) {
  std::set<std::string> names;
  const std::string needle = "class=\"";
  for (std::size_t at = cards.find(needle); at != std::string::npos; at = cards.find(needle, at + 1)) {
    const std::size_t start = at + needle.size();
    const std::size_t end = cards.find('"', start);
    if (end == std::string::npos) {
      break;
    }
    std::istringstream parts(cards.substr(start, end - start));
    std::string one;
    while (parts >> one) {
      names.insert(one);
    }
  }
  return names;
}

std::vector<std::string> tag_texts(const std::string &html, const std::string &open) {
  std::vector<std::string> out;
  for (std::size_t at = html.find(open); at != std::string::npos; at = html.find(open, at + 1)) {
    const std::size_t start = html.find('>', at);
    if (start == std::string::npos) {
      break;
    }
    const std::size_t end = html.find('<', start);
    if (end == std::string::npos) {
      break;
    }
    out.push_back(html.substr(start + 1, end - start - 1));
  }
  return out;
}

v4l2diag::RunResult contract_run() {
  v4l2diag::RunResult run;
  run.started_at_utc = "2026-08-04T12:02:38Z";
  run.finished_at_utc = "2026-08-04T12:41:07Z";
  run.host_name = "diag-host";
  run.kernel_release = "5.15.0-139-generic";
  run.threshold_config_file = "stress-test.json";
  run.trigger_mode = v4l2diag::TriggerMode::Hardware;
  run.trigger_profile_id = "anvil";
  run.trigger_profile_file = "anvil.json";
  run.run_id = "web-run-contract";

  v4l2diag::CameraRunResult camera;
  camera.camera_path = "/dev/video0";
  camera.role = "master";
  // ALL 26 test ids, with the real slugs the renderers dispatch on. A narrower fixture
  // would let three green tests stand in for twenty-six: the contract is only proven for
  // the cards the run actually rendered.
  static const char *kTestIds[] = {
      "t01-device-compliance",      "t02-control-inventory", "t03-pipeline-ready",    "t04-no-streamon",
      "t05-pollerr-handling",       "t06-stream-cycles",     "t07-multi-buffer",      "t08-buffer-overwrite",
      "t09-buffer-recycling",       "t10-buffer-flags",      "t11-memory-throughput", "t12-dmabuf-cache-sync",
      "t13-poll-timeout-cliff",     "t14-trigger-latency",   "t15-nonblock-vs-block", "t16-gpio-pulse-width",
      "t17-format-comparison",      "t18-control-sweep",     "t19-resolution-sweep",  "t20-sequence-continuity",
      "t21-timestamp-monotonicity", "t22-stuck-frame",       "t23-sustained-capture", "t24-latency-under-load",
      "t25-multi-camera",           "t26-cold-start"};
  const v4l2diag::TestStatus cycle[] = {v4l2diag::TestStatus::Pass, v4l2diag::TestStatus::Warn,
                                        v4l2diag::TestStatus::Fail};
  std::size_t at = 0;
  for (const char *id : kTestIds) {
    camera.tests.push_back(test_of(id, id, cycle[at % 3]));
    ++at;
  }
  run.cameras.push_back(camera);
  return run;
}

}  // namespace

int main() {
  const v4l2diag::RunResult run = contract_run();
  const std::string directory = make_temp_dir();
  try {
    v4l2diag::write_reports(run, directory);
  } catch (const std::exception &error) {
    std::cout << "FAIL: write_reports threw: " << error.what() << "\n";
    return 1;
  }
  const std::string html =
      read_file(directory + "/" + v4l2diag::report_artifact_filename(naming_of(run), v4l2diag::ReportFormat::Html));
  const std::string cards = cards_of(html);
  if (!check(!cards.empty(), "no test card was rendered")) {
    return 1;
  }
  const std::set<std::string> classes = card_classes(cards);

  // --- S1: tables are div+grid, never <table> ------------------------------
  // Observed: the literal tag "<table" inside the card region.
  check(count_of(cards, "<table") == 0,
        "cards still emit <table>; S1 requires div+grid (found " + std::to_string(count_of(cards, "<table")) + ")");

  // --- S1: only the canonical section names -------------------------------
  // Observed: the text of every <h3 class="section-label">.
  {
    const std::set<std::string> allowed = {"Measurement",
                                           "Measurement Result",
                                           "Test Configuration",
                                           "Control Evidence",
                                           "Device Evidence \xC2\xB7 Information",
                                           "Device Evidence \xC2\xB7 Capability",
                                           "Device Evidence \xC2\xB7 Pixel Formats"};
    for (const std::string &name : tag_texts(cards, "<h3 class=\"section-label\"")) {
      check(allowed.count(name) == 1, "section name outside the canonical set: '" + name + "'");
    }
  }

  // --- S1: no em dash in a Status cell -------------------------------------
  // Observed: a cell carrying the status class whose text is the em dash.
  check(!contains(cards, "-status\">\xE2\x80\x94<") && !contains(cards, "-outcome\">\xE2\x80\x94<"),
        "a Status cell renders an em dash; S1 splits the table instead");

  // --- S1: banned column headers ------------------------------------------
  // Observed: header cell text.
  for (const char *banned : {"Outcome", "State", "Scope"}) {
    check(!contains(cards, std::string(">") + banned + "</span>"),
          std::string("banned column header '") + banned + "' is still emitted");
  }

  // --- structure: every section closes, and no item escapes one -----------
  // Observed: <section>/</section> counts, and the nesting depth at each item heading.
  //
  // Both defects are invisible in the source: a section_close() left inside an
  // `if (data.empty())` branch balances on the run that HAS data, and an item emitted
  // before its section opens still renders -- just unindented and outside the box.
  {
    const std::size_t opened = count_of(cards, "<section");
    const std::size_t closed = count_of(cards, "</section>");
    check(opened == closed,
          "section tags do not balance: " + std::to_string(opened) + " open, " + std::to_string(closed) + " closed");
    int depth = 0;
    bool escaped = false;
    for (std::size_t at = 0; at < cards.size(); ++at) {
      if (cards.compare(at, 8, "<section") == 0) {
        ++depth;
      } else if (cards.compare(at, 10, "</section>") == 0) {
        --depth;
      } else if (cards.compare(at, 22, "<h4 class=\"item-label\"") == 0 && depth == 0) {
        escaped = true;
      }
    }
    check(!escaped, "an item-label sits outside every section, so it loses its indent and its box");
  }

  // --- S1: every card that measures anything states a verdict -------------
  // Observed: the section labels inside each <article>, per card.
  //
  // Measurement Result is the ONLY section that carries a Status column, so a card
  // without one shows numbers and never says whether they passed. T01 and T02 are
  // inventories -- they report what the device has, not whether it met a threshold --
  // and are the only cards the approved set exempts.
  {
    const std::set<std::string> inventory = {"t01-device-compliance", "t02-control-inventory"};
    std::size_t at = 0;
    while ((at = cards.find("<article class=\"test-card", at)) != std::string::npos) {
      const std::size_t end = cards.find("<article class=\"test-card", at + 1);
      const std::string card = cards.substr(at, end == std::string::npos ? std::string::npos : end - at);
      const std::size_t id_at = card.find("id=\"result-mmap-");
      std::string slug;
      if (id_at != std::string::npos) {
        const std::size_t start = id_at + 16;
        slug = card.substr(start, card.find('"', start) - start);
      }
      if (inventory.count(slug) == 0 && !slug.empty()) {
        check(contains(card, "section-label\">Measurement Result<"),
              slug + " has no Measurement Result section, so no row in it carries a verdict");
      }
      at += 1;
    }
  }

  // --- S1: a verdict row states a measured value, not "Unavailable" -------
  // Observed: the Value cell of every row inside a Measurement Result section.
  //
  // A section that exists but reads "Unavailable" down the column is the failure this
  // guards: the structure passes every shape check while the report says nothing. It
  // happens when a spec names a metric the runner does not record -- a plausible name
  // that simply never matches.
  {
    std::size_t rows = 0;
    std::size_t blank = 0;
    std::size_t at = 0;
    while ((at = cards.find("section-label\">Measurement Result<", at)) != std::string::npos) {
      const std::size_t end = cards.find("</section>", at);
      const std::string section = cards.substr(at, end == std::string::npos ? std::string::npos : end - at);
      std::size_t row_at = 0;
      while ((row_at = section.find("<div class=\"grid-row cols-6\">", row_at)) != std::string::npos) {
        const std::size_t row_end = section.find("<div class=\"grid-row", row_at + 1);
        const std::string one =
            section.substr(row_at, row_end == std::string::npos ? std::string::npos : row_end - row_at);
        ++rows;
        if (contains(one, "Unavailable")) {
          ++blank;
        }
        row_at += 1;
      }
      at += 1;
    }
    check(rows > 0, "no Measurement Result rows were rendered at all");
    check(blank == 0, std::to_string(blank) + " of " + std::to_string(rows) +
                          " verdict rows read Unavailable: the section renders but reports nothing");
  }

  // --- S1: a configurable test states the parameters it ran with ----------
  // Observed: the section labels per card, same walk as the verdict check.
  //
  // T01 and T02 are discovery probes with nothing to configure; every other card must
  // show what it was told to do, or its numbers cannot be reproduced.
  {
    const std::set<std::string> probes = {"t01-device-compliance", "t02-control-inventory"};
    std::size_t at = 0;
    while ((at = cards.find("<article class=\"test-card", at)) != std::string::npos) {
      const std::size_t end = cards.find("<article class=\"test-card", at + 1);
      const std::string card = cards.substr(at, end == std::string::npos ? std::string::npos : end - at);
      const std::size_t id_at = card.find("id=\"result-mmap-");
      std::string slug;
      if (id_at != std::string::npos) {
        const std::size_t start = id_at + 16;
        slug = card.substr(start, card.find('"', start) - start);
      }
      if (probes.count(slug) == 0 && !slug.empty()) {
        check(contains(card, "section-label\">Test Configuration<"),
              slug + " has no Test Configuration section, so its run is not reproducible");
      }
      at += 1;
    }
  }

  // --- no fabricated measurements -----------------------------------------
  // Observed: numbers in the rendered cards that no fixture metric could have produced.
  //
  // Renderers used to carry stand-in rows for the empty case -- "44.801 ms", "76.476ms",
  // "/dev/video0" -- which read as measurements while the run had recorded nothing. A
  // fabricated number in a diagnostic report is the worst failure mode here, because it
  // is indistinguishable from a real one.
  for (const char *invented :
       {"44.801", "44.805", "44.830", "76.476", "82.888", "91.574", "30.702", "43.674", "/dev/video1"}) {
    check(!contains(cards, invented), std::string("a hard-coded stand-in value is rendered: ") + invented);
  }

  // --- 5.3 / design-spec 6.10: responsive rules ---------------------------
  // Observed: the stylesheet rules that make the four approved viewports work. The
  // widths themselves were measured in Chrome (1440 / 1024 / 768 / 390, three trigger
  // modes -- 12 combinations, all with no document-level horizontal scroll, no clipped
  // chart, no overlapping header cell). These rules are what keep it that way.
  //
  // The chart is the interesting case: it renders at a FIXED 906px so a declared 8px
  // label really is 8px. Below ~1000px that no longer fits, and the box must SCROLL
  // rather than shrink -- shrinking would scale every label with it, undoing the whole
  // reason the width is fixed.
  check(contains(html, ".chart-frame, .metric-chart { overflow-x: auto; max-width: 100%; }"),
        "charts no longer scroll inside their box, so a narrow viewport clips or rescales them");
  check(contains(html, "@media (max-width: 700px)"), "the mobile breakpoint is gone");
  check(contains(html, ".test-header { grid-template-columns: 1fr; row-gap: 4px; }"),
        "the test header no longer stacks on mobile, so its three cells compete for one row");

  // --- design-spec §4: one status colour across the whole report -----------
  // Observed: the CSS rules, because a colour is only comparable once resolved.
  //
  // Test Results Overview is the reference (design-spec). Three places render a status:
  // that table, the card header, and the verdict cell. They used to carry three
  // different greens -- rgb(22,163,74), rgb(23,100,58), rgb(23,100,58) -- so the same
  // PASS looked like a different verdict depending on where the eye landed.
  //
  // Binding every one of them to the same custom property is what makes the rule hold;
  // a literal hex here is how the drift got in.
  for (const char *tone : {"pass", "warn", "fail", "skip"}) {
    const std::string card_rule = std::string(".test-card.") + tone + " {";
    const std::size_t card_at = html.find(card_rule);
    check(card_at != std::string::npos, std::string("no card colour rule for ") + tone);
    if (card_at != std::string::npos) {
      const std::string block = html.substr(card_at, html.find('}', card_at) - card_at);
      check(block.find(std::string("var(--") + tone + ")") != std::string::npos,
            std::string("the ") + tone + " card header does not use var(--" + tone +
                "), so it can drift "
                "from the Overview table");
    }
    const std::string cell_rule = std::string(".grid-row .verdict.") + tone + " {";
    const std::size_t cell_at = html.find(cell_rule);
    check(cell_at != std::string::npos, std::string("no verdict cell colour rule for ") + tone);
    if (cell_at != std::string::npos) {
      const std::string block = html.substr(cell_at, html.find('}', cell_at) - cell_at);
      check(block.find(std::string("var(--") + tone + ")") != std::string::npos,
            std::string("the ") + tone + " verdict cell does not use var(--" + tone + ")");
    }
  }

  // --- S2: item subheadings exist -----------------------------------------
  // Observed: the CSS class the previews use for a chart/table subheading.
  check(classes.count("item-label") == 1, "no item-label subheading is emitted; S2 requires one per chart/table");

  // --- S3: the staircase is scoped by has-items ---------------------------
  check(classes.count("has-items") == 1, "no has-items class; S3 scopes the 21/53/69px staircase with it");

  // --- S4: unit vocabulary -------------------------------------------------
  // Observed: the text of every cell carrying a unit class.
  {
    const std::set<std::string> real = {"milliseconds",
                                        "microseconds",
                                        "seconds",
                                        "nanoseconds",
                                        "percent",
                                        "hertz",
                                        "pixels",
                                        "bytes",
                                        "kibibytes",
                                        "mebibytes",
                                        "gibibytes",
                                        "frames per second",
                                        "mebibytes per second",
                                        "gibibytes per second",
                                        "milliseconds per buffer",
                                        "buffers per second",
                                        "returns per frame",
                                        "\xE2\x80\x94"};
    for (const std::string &unit : tag_texts(cards, "<span class=\"cfg-unit\"")) {
      check(real.count(unit) == 1, "Test Configuration unit outside the vocabulary: '" + unit + "'");
    }
  }

  // --- design-spec: banned blocks -----------------------------------------
  check(!contains(cards, "Metric definitions") && !contains(cards, "Metric Definitions"),
        "the Metric Definitions glossary is still rendered");
  check(!contains(cards, "Recorded values") && !contains(cards, "RECORDED VALUES"),
        "the raw Recorded values dump is still rendered");

  // --- S8: the CSS pins the rendered width to the viewBox width -----------
  // Observed: the stylesheet rule, because the scale can only be measured in a browser.
  // A live Chrome run confirmed 16/16 charts at scale 1.0000 with this rule present; the
  // rule is what keeps it there, so its absence is the regression to catch.
  check(contains(html, "width: 906px; min-width: 906px"),
        "the chart width is no longer pinned to the viewBox width, so labels rescale");

  // --- S8: SVG viewBox equals the render width ----------------------------
  // Observed: the viewBox attribute of every chart.
  {
    const std::string needle = "viewBox=\"0 0 ";
    for (std::size_t at = cards.find(needle); at != std::string::npos; at = cards.find(needle, at + 1)) {
      const std::size_t start = at + needle.size();
      const std::size_t end = cards.find(' ', start);
      const std::string width = cards.substr(start, end - start);
      // The stream may print the width as "906.000"; the number is what matters.
      const std::string trimmed = width.substr(0, width.find('.'));
      check(trimmed == "906" || trimmed == "938", "SVG viewBox width is '" + width + "'; S8 wants 906 or 938");
    }
  }

  unlink((directory + "/" + v4l2diag::report_artifact_filename(naming_of(run), v4l2diag::ReportFormat::Html)).c_str());
  unlink((directory + "/" + v4l2diag::report_artifact_filename(naming_of(run), v4l2diag::ReportFormat::Json)).c_str());
  unlink(
      (directory + "/" + v4l2diag::report_artifact_filename(naming_of(run), v4l2diag::ReportFormat::Markdown)).c_str());
  rmdir(directory.c_str());

  if (failures == 0) {
    std::cout << "report card contract: all checks passed\n";
    return 0;
  }
  std::cout << failures << " contract check(s) failed\n";
  return 1;
}
