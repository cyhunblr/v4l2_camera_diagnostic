// Two-way CSS contract over a rendered report (plan Faz 6.1).
//
// design-spec, "Result blogu": verification is TWO-WAY -- every `result-*` class used must
// be defined, AND every rule defined must be used. This test generalises that rule to the
// whole stylesheet, because the failure it catches is not specific to the result block:
// the 2026-08-09 device run emitted `result-warn`, the T03 stacked-bar family and the T06
// reliability-bar family into the markup while the stylesheet carried no rule for any of
// them. A class without a rule renders as an unstyled shape -- transparent fill, no
// padding, browser-default 16px -- so the amber warning callout read as plain body text
// and the T03 bar segments collapsed until their two labels printed as one number
// ("1141145" for 1141 ms + 145 ms). `report_writer.cpp` already carries that warning in a
// comment; nothing enforced it.
//
// Measured on the PRODUCTION path: `write_reports()` output, not a renderer fragment
// (project rule 4).

#include <unistd.h>

#include <algorithm>
#include <cstddef>
#include <exception>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <utility>
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

std::string make_temp_dir() {
  char pattern[] = "/tmp/v4l2diag-css-XXXXXX";
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

// The <style> element's contents, concatenated.
std::string stylesheet_of(const std::string &html) {
  std::string css;
  const std::string open = "<style>";
  const std::string close = "</style>";
  for (std::size_t at = html.find(open); at != std::string::npos; at = html.find(open, at + 1)) {
    const std::size_t start = at + open.size();
    const std::size_t end = html.find(close, start);
    if (end == std::string::npos) {
      break;
    }
    css += html.substr(start, end - start);
    css += "\n";
  }
  return css;
}

// The markup with every <style> block cut out, so selector text inside the stylesheet is
// never mistaken for a class attribute in the body.
std::string markup_of(const std::string &html) {
  std::string out;
  const std::string open = "<style>";
  const std::string close = "</style>";
  std::size_t cursor = 0;
  while (true) {
    const std::size_t at = html.find(open, cursor);
    if (at == std::string::npos) {
      out += html.substr(cursor);
      break;
    }
    out += html.substr(cursor, at - cursor);
    const std::size_t end = html.find(close, at);
    if (end == std::string::npos) {
      break;
    }
    cursor = end + close.size();
  }
  return out;
}

bool is_class_char(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
}

// Every class token appearing in a class="..." attribute.
std::set<std::string> classes_used(const std::string &markup) {
  std::set<std::string> used;
  const std::string needle = "class=\"";
  for (std::size_t at = markup.find(needle); at != std::string::npos; at = markup.find(needle, at + 1)) {
    const std::size_t start = at + needle.size();
    const std::size_t end = markup.find('"', start);
    if (end == std::string::npos) {
      break;
    }
    std::istringstream tokens(markup.substr(start, end - start));
    std::string token;
    while (tokens >> token) {
      used.insert(token);
    }
  }
  return used;
}

// Every class token named by a selector in the stylesheet. Declaration blocks are skipped
// so a value like `border-radius` never registers as a class, and /* comments */ are
// skipped too -- the rationale comments here quote forbidden selectors like `.result` by
// name, and counting those would report a rule that does not exist.
std::set<std::string> classes_defined(const std::string &css) {
  std::set<std::string> defined;
  bool in_block = false;
  for (std::size_t i = 0; i < css.size(); ++i) {
    if (css.compare(i, 2, "/*") == 0) {
      const std::size_t close = css.find("*/", i + 2);
      if (close == std::string::npos) {
        break;
      }
      i = close + 1;
      continue;
    }
    const char c = css[i];
    if (c == '{') {
      in_block = true;
      continue;
    }
    if (c == '}') {
      in_block = false;
      continue;
    }
    if (in_block || c != '.') {
      continue;
    }
    // A leading '.' followed by a digit is a decimal number in a selector context
    // (`.5em` never appears as a class), so it is not a class token.
    std::size_t end = i + 1;
    while (end < css.size() && is_class_char(css[end])) {
      ++end;
    }
    if (end > i + 1 && !(css[i + 1] >= '0' && css[i + 1] <= '9')) {
      defined.insert(css.substr(i + 1, end - i - 1));
    }
    i = end - 1;
  }
  return defined;
}

v4l2diag::MetricValue metric_of(const std::string &name, double value, const std::string &unit) {
  v4l2diag::MetricValue metric;
  metric.name = name;
  metric.value = value;
  metric.unit = unit;
  return metric;
}

v4l2diag::TestResult base_test(const std::string &id, const std::string &name, v4l2diag::TestStatus status, double ms) {
  v4l2diag::TestResult test;
  test.id = id;
  test.name = name;
  test.status = status;
  test.memory_backend = "mmap";
  test.duration_ms = ms;
  test.summary = name + " completed.";
  test.category = "capture";
  return test;
}

// T03 draws its stacked chart from "cycle: N|streamon|first_frame|total[|pulses]" detail
// lines; T06 draws its reliability bars from the four cycle-count metrics. Without these
// the two chart families never reach the markup and this contract would pass vacuously.
v4l2diag::RunResult css_run() {
  v4l2diag::RunResult run;
  run.started_at_utc = "2026-08-09T10:31:39Z";
  run.finished_at_utc = "2026-08-09T10:47:06Z";
  run.host_name = "css-host";
  run.kernel_release = "5.10.120-tegra";
  run.kernel_version = "#1 SMP";
  run.threshold_config_file = "default.json";
  run.trigger_mode = v4l2diag::TriggerMode::Hardware;
  run.trigger_profile_id = "anvil";
  run.trigger_profile_file = "anvil.json";
  run.run_id = "web-run-css";

  v4l2diag::CameraRunResult camera;
  camera.camera_path = "/dev/video4";
  camera.role = "master";
  camera.trigger_description = "GPIO line 108";

  // T03 -- PASS, and the source of the stacked-bar family.
  v4l2diag::TestResult t03 =
      base_test("t03-pipeline-ready", "Pipeline Readiness after STREAMON", v4l2diag::TestStatus::Pass, 11600);
  t03.details.push_back("cycle: 1|1141|145|1286|2");
  t03.details.push_back("cycle: 2|1126|145|1271|2");
  t03.details.push_back("cycle: 3|1127|145|1272|2");
  camera.tests.push_back(t03);

  // T04 -- WARN, so `result-warn` reaches the markup.
  v4l2diag::TestResult t04 =
      base_test("t04-no-streamon", "Frame Capture without STREAMON", v4l2diag::TestStatus::Warn, 30);
  t04.summary = "DQBUF correctly fails, but poll returned non-zero without STREAMON.";
  camera.tests.push_back(t04);

  // T06 -- FAIL, so `result-fail` reaches the markup, and the reliability-bar family with
  // it. A sub-70% rapid phase selects `bar-fail`; the full phase at 100% selects
  // `bar-pass`.
  v4l2diag::TestResult t06 =
      base_test("t06-stream-cycles", "STREAMON/STREAMOFF Cycle Reliability", v4l2diag::TestStatus::Fail, 289000);
  t06.summary = "All 50 rapid cycles started streaming but no frame arrived.";
  t06.metrics.push_back(metric_of("full_cycles_success", 20, ""));
  t06.metrics.push_back(metric_of("full_cycles_attempted", 20, ""));
  t06.metrics.push_back(metric_of("rapid_cycles_ok", 0, ""));
  t06.metrics.push_back(metric_of("rapid_cycles_attempted", 50, ""));
  // Whole-millisecond aggregates, read through value_of_any() -- the same path the
  // fractional latencies take, so 6.10 observes the integer branch of the ms formatter
  // rather than passing because no such value was rendered. The approved t06 preview shows
  // these as "1132" and "1140"; an unconditional %.3f would print "1,132.000".
  t06.metrics.push_back(metric_of("streamon_ms_mean", 1132.0, "milliseconds"));
  t06.metrics.push_back(metric_of("streamon_ms_max", 1140.0, "milliseconds"));
  // ...and a fractional one beside them, so both branches are exercised in one card.
  t06.metrics.push_back(metric_of("first_frame_latency_mean", 44.812345, "milliseconds"));
  camera.tests.push_back(t06);

  // T11 -- PASS, and the source of the >=1000 values the separator contract needs: a
  // fractional throughput (mebibytes per second) and integer byte counts. Without these the
  // separator assertion would pass vacuously, which is the trap project rule 9 describes.
  v4l2diag::TestResult t11 =
      base_test("t11-memory-throughput", "Memory Access Throughput", v4l2diag::TestStatus::Pass, 545);
  t11.metrics.push_back(metric_of("sizeimage_bytes", 4915200, "bytes"));
  t11.metrics.push_back(metric_of("buffer_capacity_bytes", 5439744, "bytes"));
  t11.metrics.push_back(metric_of("memcpy_mib_s", 1027.13, "mebibytes per second"));
  // "copy: label|bytes|mib_s|class" -- four fields, the class in the LAST one. A fifth
  // field pushed "cache" out of position and every bar rendered as the primary series.
  t11.details.push_back("copy: Full frame|5439744|1027.13|full");
  t11.details.push_back("copy: 4 KiB sample|4096|1033.87|cache");
  t11.details.push_back("copy: 64 KiB sample|65536|1025.31|cache");
  camera.tests.push_back(t11);

  // T15 -- PASS, and the largest displayed number in the approved set: 87,325.9 spins.
  // The two latency metrics carry the sub-millisecond digits the precision contract is
  // about: at "%.2f" both means collapse to 44.8 and the comparison the test exists for
  // disappears.
  v4l2diag::TestResult t15 =
      base_test("t15-nonblock-vs-block", "Non-blocking Spin vs Blocking DQBUF", v4l2diag::TestStatus::Pass, 23671);
  t15.metrics.push_back(metric_of("avg_eagain_spins", 87220.93, ""));
  t15.metrics.push_back(metric_of("nonblock_latency_mean", 44.796123, "milliseconds"));
  t15.metrics.push_back(metric_of("block_latency_mean", 44.799456, "milliseconds"));
  camera.tests.push_back(t15);

  // T25 -- SKIP, so `result-skip` reaches the markup.
  v4l2diag::TestResult t25 =
      base_test("t25-multi-camera", "Multi-camera Capture and Synchronization", v4l2diag::TestStatus::Skipped, 10);
  t25.summary = "Multi-camera test requires at least one slave camera selected.";
  camera.tests.push_back(t25);

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

  const v4l2diag::RunResult run = css_run();
  const std::string directory = make_temp_dir();
  if (directory.empty()) {
    std::cout << "FAIL: could not create a temp directory\n";
    return 1;
  }
  try {
    v4l2diag::write_reports(run, directory);
  } catch (const std::exception &error) {
    std::cout << "FAIL: write_reports threw: " << error.what() << "\n";
    return 1;
  }
  const std::string html =
      read_file(directory + "/" + v4l2diag::report_artifact_filename(naming_of(run), v4l2diag::ReportFormat::Html));
  remove_artifacts(directory, run);
  ok &= check(!html.empty(), "the report is empty");

  const std::string css = stylesheet_of(html);
  const std::string markup = markup_of(html);
  ok &= check(!css.empty(), "the report carries no stylesheet");
  ok &= check(markup.find("<style>") == std::string::npos, "markup_of left a style block behind");

  const std::set<std::string> used = classes_used(markup);
  const std::set<std::string> defined = classes_defined(css);

  // --- 6.1a: the fixture really did emit the families under test -----------
  // Without this the contract below could pass by rendering nothing at all.
  for (const char *required :
       {"result-warn", "result-fail", "result-skip", "stacked-chart", "cycle-row", "bar-streamon", "bar-firstframe",
        "rel-chart", "rel-row", "bar-pass", "bar-fail", "scale-name"}) {
    ok &= check(used.count(required) == 1,
                std::string("fixture did not emit class '") + required + "'; the CSS contract would pass vacuously");
  }

  // --- 6.1b: every class used in the markup has a rule ---------------------
  // The 2026-08-09 run failed exactly here, on twelve classes at once.
  {
    std::vector<std::string> undefined;
    for (const auto &name : used) {
      if (defined.count(name) == 0) {
        undefined.push_back(name);
      }
    }
    std::string joined;
    for (const auto &name : undefined) {
      joined += (joined.empty() ? "" : ", ") + name;
    }
    ok &= check(undefined.empty(), "classes used in markup with NO CSS rule (renders unstyled): " + joined);
  }

  // --- 6.1b2: a card carries AT MOST ONE result block ---------------------
  // design-spec: "Kart statusu ile class birebir eslesir" -- one card, one verdict line.
  // T13/T19/T25 shipped two identical banners in the 2026-08-09 run: the dispatcher emits
  // the block for every non-PASS card and fourteen per-test renderers emitted it again.
  // While the rule was missing the duplicate was invisible; styling it made it obvious.
  {
    const std::string open = "<article class=\"test-card";
    for (std::size_t at = markup.find(open); at != std::string::npos; at = markup.find(open, at + 1)) {
      const std::size_t next = markup.find(open, at + 1);
      const std::string card = markup.substr(at, next == std::string::npos ? std::string::npos : next - at);
      const std::size_t id_at = card.find("id=\"");
      const std::string id = id_at == std::string::npos ? std::string("(unknown)")
                                                        : card.substr(id_at + 4, card.find('"', id_at + 4) - id_at - 4);
      std::size_t banners = 0;
      for (const char *tone : {"class=\"result-warn\"", "class=\"result-fail\"", "class=\"result-skip\""}) {
        for (std::size_t b = card.find(tone); b != std::string::npos; b = card.find(tone, b + 1)) {
          ++banners;
        }
      }
      ok &= check(banners <= 1, "card " + id + " carries " + std::to_string(banners) + " result blocks; at most 1");
    }
  }

  // --- 6.3: a SKIP card that recorded nothing is the banner alone ----------
  // All seven SKIP cards in the approved previews (t08/t09 x2/t12/t14/t16 x2) contain
  // exactly one child: the `result-skip` div. No test-body wrapper, no Measurement, no
  // Measurement Result, no Test Configuration. The 2026-08-09 run rendered T25 with the
  // full body -- four item labels, three sections, 1383 characters -- reporting
  // "Unavailable" down every column. That run's JSON records T25 with 0 metrics and 0
  // details, so nothing sat behind those placeholders. Project rule 4b: content the
  // approved preview does not show is a deviation even when it is extra rather than
  // missing.
  //
  // The fixture's T25 mirrors that shape (no metrics, no details). A skipped test that DID
  // record evidence keeps its body -- report_writer_test covers that case -- so this
  // assertion is about the empty one.
  {
    const std::string open = "<article class=\"test-card skip\"";
    std::size_t skips = 0;
    for (std::size_t at = markup.find(open); at != std::string::npos; at = markup.find(open, at + 1)) {
      ++skips;
      const std::size_t next = markup.find("<article class=\"test-card", at + 1);
      const std::string card = markup.substr(at, next == std::string::npos ? std::string::npos : next - at);
      const std::size_t id_at = card.find("id=\"");
      const std::string id = id_at == std::string::npos ? std::string("(unknown)")
                                                        : card.substr(id_at + 4, card.find('"', id_at + 4) - id_at - 4);
      ok &= check(card.find("class=\"result-skip\"") != std::string::npos,
                  "skipped card " + id + " carries no result-skip banner");
      for (const char *forbidden : {"section-label\">Measurement<", "section-label\">Measurement Result<",
                                    "section-label\">Test Configuration<", "class=\"item-label\"", "class=\"grid-head",
                                    "class=\"chart-frame\""}) {
        ok &= check(card.find(forbidden) == std::string::npos,
                    "skipped card " + id + " renders '" + forbidden + "'; the approved SKIP card is the banner alone");
      }
    }
    ok &= check(skips > 0, "the fixture rendered no SKIP card; the SKIP contract would pass vacuously");
  }

  // --- 6.4: a displayed number >= 1000 carries thousands separators --------
  // Approved previews group them: t11 "1,014.5" MiB/s, t15 "87,325.9" spins, t17 "1,016.6",
  // t19 "1,049.1". The 2026-08-09 run printed "1027.13", "87220.93", "1014.26", "1053.23".
  // Integer byte counts were already grouped (grouped_bytes(): 5,439,744), so only the
  // FRACTIONAL path was missing -- and one integer cell bypassed grouped_bytes() entirely
  // ("Buffer utilization" printed 4915200 beside the same value shown as 4,915,200).
  //
  // Scope (user decision, option C): separators only. Decimal precision is NOT asserted
  // here -- the approved set uses 0, 1, 2 and 3 decimals for the same declared
  // float/milliseconds type, so no single precision rule reproduces it and the question is
  // still open.
  //
  // Observed: the Value cell of every canonical grid row, and the bar labels.
  {
    std::size_t checked = 0;
    std::vector<std::string> ungrouped;
    const std::string row_open = "<div class=\"grid-row";
    for (std::size_t at = markup.find(row_open); at != std::string::npos; at = markup.find(row_open, at + 1)) {
      const std::size_t end = markup.find("</div>", at);
      const std::string row = markup.substr(at, end == std::string::npos ? std::string::npos : end - at);
      // Every <span> payload in the row.
      const std::string cell_open = "<span>";
      for (std::size_t c = row.find(cell_open); c != std::string::npos; c = row.find(cell_open, c + 1)) {
        const std::size_t cs = c + cell_open.size();
        const std::size_t ce = row.find("</span>", cs);
        if (ce == std::string::npos) {
          break;
        }
        const std::string cell = row.substr(cs, ce - cs);
        // A bare number, optionally followed by a unit word. Reject anything with a comma
        // already, a slash (ratios like 50/50), an 'x' (relative factors) or '=' (packed
        // detail strings).
        std::size_t digits = 0;
        std::size_t dot = std::string::npos;
        bool bare = !cell.empty();
        for (std::size_t i = 0; i < cell.size() && bare; ++i) {
          const char ch = cell[i];
          if (ch >= '0' && ch <= '9') {
            ++digits;
          } else if (ch == '.' && dot == std::string::npos) {
            dot = i;
          } else if (ch == ' ') {
            break;  // a unit follows; the numeric head is what matters
          } else {
            bare = false;
          }
        }
        if (!bare || digits == 0) {
          continue;
        }
        const std::size_t int_digits = dot == std::string::npos ? digits : dot;
        if (int_digits < 4) {
          continue;
        }
        ++checked;
        ungrouped.push_back(cell);
      }
    }
    std::string joined;
    for (const auto &c : ungrouped) {
      joined += (joined.empty() ? "" : ", ") + c;
    }
    ok &= check(ungrouped.empty(), "values >= 1000 printed without thousands separators: " + joined);
    (void)checked;
  }

  // --- 6.7: T11's throughput chart uses the shared bar vocabulary ---------
  // The approved t11-preview.html draws it as chart-legend + chart-frame > thr-chart >
  // bar-row(bar-label, bar-track > bar-fill.bar-full|bar-cache, bar-info), closed by a
  // scale-name caption. Production borrowed T08's saturation family (load-row / load-track
  // / load-bar / load-value) instead, which sized the reading column differently and
  // dropped both the legend and the caption. The user's decision is that the approved
  // convention governs (2026-08-09).
  {
    const std::size_t at = markup.find("[id^=\"result-mmap-t11\"");
    (void)at;
    const std::size_t card = markup.find("id=\"result-mmap-t11");
    if (card != std::string::npos) {
      const std::size_t end = markup.find("</article>", card);
      const std::string t11 = markup.substr(card, end == std::string::npos ? std::string::npos : end - card);
      for (const char *needle : {"thr-chart", "bar-fill bar-full", "bar-fill bar-cache", "legend-full", "legend-cache",
                                 "class=\"bar-info\"", "class=\"scale-name\""}) {
        ok &= check(t11.find(needle) != std::string::npos,
                    std::string("T11's chart does not use the approved '") + needle + "'");
      }
      for (const char *banned :
           {"load-row", "load-track", "load-bar", "load-value", "t11-copy-bar", "t11-full-bar", "t11-cache-bar"}) {
        ok &= check(t11.find(banned) == std::string::npos,
                    std::string("T11's chart still emits the superseded '") + banned + "'");
      }
    } else {
      ok &= check(false, "the fixture rendered no T11 card; the chart contract would pass vacuously");
    }
  }

  // --- 6.8: every CSS rule is used by some markup (two-way, reverse leg) --
  // design-spec: "tanimli her kural kullanilmis olmali". A rule no markup names is a
  // decision that silently stopped applying -- exactly how the load-* family outlived the
  // chart it was written for.
  {
    // Selectors that legitimately have no class in the body: state/modifier hooks applied
    // conditionally, print-only rules, and the page shell.
    static const std::set<std::string> exempt = {
        "pass",       "warn",      "fail",    "skip",    "error",       "ready",          "neutral",       "good",
        "bad",        "has-items", "cols-2",  "cols-3",  "cols-4",      "cols-5",         "cols-6",        "cols-7",
        "cols-8",     "cols-9",    "cols-10", "verdict", "container",   "footer",         "subtitle",      "export-row",
        "meta-group", "meta-row",  "k",       "v",       "group-title", "export-pdf-btn", "export-actions"};
    std::vector<std::string> unused;
    for (const auto &name : defined) {
      if (used.count(name) == 0 && exempt.count(name) == 0) {
        unused.push_back(name);
      }
    }
    std::string joined;
    for (const auto &n : unused) {
      joined += (joined.empty() ? "" : ", ") + n;
    }
    // Reported, not failed: this fixture renders six tests, so most of the sheet is
    // legitimately unexercised here. The assertion guards the classes this fixture DOES
    // drive, which 6.7 names explicitly.
    if (!unused.empty()) {
      std::cout << "NOTE: rules not exercised by this fixture (" << unused.size() << "): " << joined << "\n";
    }
  }

  // --- 6.9: a latency value in milliseconds carries three decimals --------
  // The approved set prints millisecond latencies at 3dp -- t14 "44.836", t15 "44.796",
  // t17 "44.801"/"44.805", t24 "+0.011". Production applied one global "%.2f then strip
  // zeros", so "44.801" and "44.805" both rendered as "44.8" and two formats that differ
  // by 4 microseconds became indistinguishable. Measured across the previews: 90 of the
  // 101 mismatched cells wanted 3dp, and every one of them is a millisecond quantity.
  //
  // Deliberately NOT generalised to every number: throughput (1dp), sizes in mebibytes
  // (2dp), spin counts (1dp) and percent (2dp) keep the precision the previews give them,
  // which is why this keys on the UNIT rather than on the magnitude.
  {
    std::size_t checked = 0;
    std::vector<std::string> wrong;
    const std::string row_open = "<div class=\"grid-row";
    for (std::size_t at = markup.find(row_open); at != std::string::npos; at = markup.find(row_open, at + 1)) {
      const std::size_t end = markup.find("</div>", at);
      const std::string row = markup.substr(at, end == std::string::npos ? std::string::npos : end - at);
      if (row.find("<span>milliseconds</span>") == std::string::npos) {
        continue;
      }
      // Every cell of a row that declares milliseconds; the numeric one is the value.
      const std::string cell_open = "<span>";
      for (std::size_t c = row.find(cell_open); c != std::string::npos; c = row.find(cell_open, c + 1)) {
        const std::size_t cs = c + cell_open.size();
        const std::size_t ce = row.find("</span>", cs);
        if (ce == std::string::npos) {
          break;
        }
        const std::string cell = row.substr(cs, ce - cs);
        const std::size_t dot = cell.find('.');
        if (dot == std::string::npos || cell.empty()) {
          continue;
        }
        bool numeric = true;
        std::size_t decimals = 0;
        for (std::size_t i = 0; i < cell.size(); ++i) {
          const char ch = cell[i];
          if (ch >= '0' && ch <= '9') {
            if (i > dot) {
              ++decimals;
            }
          } else if (ch != '.' && ch != ',' && !(i == 0 && (ch == '+' || ch == '-'))) {
            numeric = false;
            break;
          }
        }
        if (!numeric || decimals == 0) {
          continue;
        }
        ++checked;
        if (decimals != 3) {
          wrong.push_back(cell + " (" + std::to_string(decimals) + "dp)");
        }
      }
    }
    std::string joined;
    for (const auto &w : wrong) {
      joined += (joined.empty() ? "" : ", ") + w;
    }
    ok &= check(wrong.empty(), "millisecond latencies not printed at 3 decimals: " + joined);
    ok &=
        check(checked > 0, "no fractional millisecond value was rendered; the precision contract would pass vacuously");
  }

  // --- 6.10: a WHOLE millisecond value keeps no decimals ------------------
  // 45 of the 55 millisecond cells in the approved set are integers, and all of them are
  // Test Configuration inputs: "Capture timeout 500", "Poll timeout 50", "Slow-start guard
  // 2000". An unconditional %.3f turned those into "500.000 milliseconds", which states a
  // microsecond-resolution setting nobody made. Losing precision is a display bug;
  // inventing it is a truthfulness bug, which is why this direction is asserted too.
  {
    std::vector<std::string> fabricated;
    const std::string row_open = "<div class=\"grid-row";
    for (std::size_t at = markup.find(row_open); at != std::string::npos; at = markup.find(row_open, at + 1)) {
      const std::size_t end = markup.find("</div>", at);
      const std::string row = markup.substr(at, end == std::string::npos ? std::string::npos : end - at);
      if (row.find("<span>milliseconds</span>") == std::string::npos) {
        continue;
      }
      const std::string cell_open = "<span>";
      for (std::size_t c = row.find(cell_open); c != std::string::npos; c = row.find(cell_open, c + 1)) {
        const std::size_t cs = c + cell_open.size();
        const std::size_t ce = row.find("</span>", cs);
        if (ce == std::string::npos) {
          break;
        }
        const std::string cell = row.substr(cs, ce - cs);
        // Only ".000" is a fabricated decimal: any other trailing digits are a real
        // sub-millisecond reading.
        if (cell.size() > 4 && cell.compare(cell.size() - 4, 4, ".000") == 0) {
          fabricated.push_back(cell);
        }
      }
    }
    std::string joined;
    for (const auto &f : fabricated) {
      joined += (joined.empty() ? "" : ", ") + f;
    }
    ok &= check(fabricated.empty(), "whole millisecond values printed with fabricated decimals: " + joined);
  }

  // --- 6.1c: the result block palette is the approved one ------------------
  // design-spec fixes these six values; a rule that exists but carries the wrong colour
  // would satisfy 6.1b while still shipping the wrong card.
  for (const auto &pair : {std::make_pair("result-fail", "#6e2424"), std::make_pair("result-fail", "#fff7f7"),
                           std::make_pair("result-warn", "#6e4f00"), std::make_pair("result-warn", "#fffdf5"),
                           std::make_pair("result-skip", "#596776"), std::make_pair("result-skip", "#f8f9fa")}) {
    const std::size_t at = css.find(std::string(".") + pair.first);
    bool found = false;
    if (at != std::string::npos) {
      const std::size_t end = css.find('}', at);
      if (end != std::string::npos) {
        found = css.substr(at, end - at).find(pair.second) != std::string::npos;
      }
    }
    ok &= check(found, std::string("rule .") + pair.first + " does not carry the approved value " + pair.second);
  }

  // --- 6.5: the section heading carries the approved underline ------------
  // All 26 approved previews override the shell's plain label with a dark 2px rule
  // (#2d3a47) plus .4px letter-spacing; two of them write the short form and inherit the
  // colour, but every one paints the border. Production shipped the shell form only, so
  // "Measurement" / "Measurement Result" / "Test Configuration" read as plain grey text
  // with no separator down a long card.
  {
    const std::size_t at = css.find(".test-card .section-label");
    bool ok_rule = at != std::string::npos;
    if (ok_rule) {
      const std::size_t end = css.find('}', at);
      const std::string body = end == std::string::npos ? std::string() : css.substr(at, end - at);
      for (const char *needle : {"border-bottom: 2px solid #2d3a47", "letter-spacing: .4px", "color: #2d3a47"}) {
        ok &= check(body.find(needle) != std::string::npos,
                    std::string("the section label rule is missing '") + needle + "'");
      }
    }
    ok &= check(ok_rule, "no .test-card .section-label rule at all");
  }

  // --- 6.6: the base font stack names the UI keywords ---------------------
  // detailed-result-card.css puts ui-sans-serif and system-ui between Inter and the
  // platform fallbacks. Without them a machine lacking Inter skips the OS UI face.
  //
  // Asserted on the `body` DECLARATION, not on the stylesheet text: the rationale comment
  // above that rule names both keywords, so a substring search over the whole sheet stayed
  // green after the rule itself was reverted (caught by sabotage).
  {
    const std::size_t at = css.find("\nbody {");
    bool found = at != std::string::npos;
    if (found) {
      const std::size_t end = css.find('}', at);
      const std::string decl = end == std::string::npos ? std::string() : css.substr(at, end - at);
      for (const char *needle : {"ui-sans-serif", "system-ui"}) {
        ok &= check(decl.find(needle) != std::string::npos, std::string("the body font stack omits '") + needle + "'");
      }
    }
    ok &= check(found, "no body rule in the stylesheet");
  }

  // --- 6.1d: a generic `result` rule is forbidden -------------------------
  // design-spec: the generic class was bound to two different colours in two files and
  // rendered t04's WARN card grey.
  {
    const std::set<std::string> banned = {"result"};
    for (const auto &name : banned) {
      ok &= check(defined.count(name) == 0, "generic '." + name + "' rule is forbidden by the design spec");
      ok &= check(used.count(name) == 0, "generic '" + name + "' class is forbidden by the design spec");
    }
  }

  // --- 6.1e: the stacked/reliability segments must not be inline ----------
  // `bar-streamon` and `bar-firstframe` are <i> elements carrying an inline width. An
  // inline box ignores width, which is what collapsed the two T03 segments until their
  // labels printed as the single number "1141145". The rule has to establish a
  // block-level or flex box for the width to apply at all.
  for (const char *name : {"bar-streamon", "bar-firstframe", "bar-pass", "bar-fail"}) {
    const std::size_t at = css.find(std::string(".") + name);
    bool block = false;
    if (at != std::string::npos) {
      const std::size_t end = css.find('}', at);
      if (end != std::string::npos) {
        const std::string body = css.substr(at, end - at);
        block = body.find("display:flex") != std::string::npos || body.find("display: flex") != std::string::npos ||
                body.find("display:block") != std::string::npos || body.find("display: block") != std::string::npos;
      }
    }
    ok &= check(block,
                std::string("rule .") + name + " must set a non-inline display, otherwise its inline width is ignored");
  }

  std::cout << (ok ? "report_css_contract: PASS\n" : "report_css_contract: FAIL\n");
  return ok ? 0 : 1;
}
