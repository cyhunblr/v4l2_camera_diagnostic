// Test Configuration must list the settings that produced the report (Faz 2 + Faz 3).
//
// The section answers one question -- "what settings produced this report" -- so its Source column
// reads `param` / `threshold`. Two findings drove this test, both from device runs:
//
//   Faz 1 (2026-08-11): 24 per-iteration MEASUREMENTS were listed as configuration variables.
//     The user reported it as "Cycle 1 diye variable olamaz". Fixed by shape-filtering the keys;
//     that check lives in test_content_registry_test.cpp.
//   Faz 2 + 3 (this test): of the 154 rows the approved previews specify across 23 tests, 106
//     were missing or misnamed. Six tests recorded no configuration at all and printed "No
//     parameters were recorded", while every one of them had already resolved its parameters
//     through tpv()/thv() -- the values were in the runner's locals and never written down.
//
// The expectations below are read from docs/assets/refactored_previews/*.html: every
// `<div class="config-row">` label of each preview's HARDWARE-TRIGGER card, in order. The previews
// are the design authority (project rule 6), so this table is transcribed, not invented.
//
// Measured on the PRODUCTION path: the runner records, the renderer reads, and the assertion looks
// at the rendered Variable cells. A renderer-only fixture would pass while a real run stayed
// empty, which is exactly how the gap survived this long.

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "v4l2diag/core/diagnostic_runner.hpp"
#include "v4l2diag/core/test_content.hpp"
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

struct ExpectedRow {
  const char *label;
  // The Source cell the approved card shows: "param", "threshold", "derived" or "fixed".
  //
  // Checked because the renderer used to INFER this from the label spelling -- "threshold" or
  // "limit" in the text meant threshold, everything else param. The 2026-08-12 02:24 device run
  // showed what that costs: 13 approved thresholds rendered as `param` (t05's "Minimum recovery",
  // t08's and t10's "Max error flags", t13's "Configured safe margin", t20's "Max allowed gaps",
  // t21's "Max non-monotonic events", t26's "Latency tolerance", t09's "Min safe cliff delay"),
  // and the `fixed` kind the previews use for method constants was never emitted at all.
  const char *source;
  // Type, Unit and Value as the approved card shows them, checked alongside Variable and Source.
  //
  // Three of the five cells were unchecked until the 2026-08-12 measurement, which found 35 rows
  // deviating: 16 thresholds printed a bare number where the design states a DIRECTION ("500" for
  // "<= 500", so the reader cannot tell a ceiling from a floor), 5 method constants carried the
  // code's wording instead of the design's ("CLOCK_MONOTONIC" for "Monotonic"), and 2 rows were
  // bound to the wrong key entirely (t26's "Latency tolerance" read a millisecond interval for a
  // percentage tolerance).
  //
  // Eight rows deliberately expect TODAY'S value rather than the design's:
  //
  //   - Five `param` rows (four "Capture timeout" defaults, t19's "Samples per resolution"). A
  //     settings row states what THIS run used, so a preview authored with other numbers is not a
  //     deviation -- forcing the default to match it would change how runs behave, not what the
  //     card says.
  //   - t13's "Production timeout" and t14's "Pulse width" are resolved per run; both match the
  //     design on a device, and only the parameter-table default differs.
  //   - t25's "Capture FAIL limit" shows 90 where the design says "< 95". Left alone because
  //     run_multi_camera has NO capture-rate rule at all -- its verdict is jitter p95 only -- so
  //     either number states a criterion the code does not apply. Reported as a gap, not painted
  //     over.
  //
  // t20's "Max allowed gaps" and t25's two sync limits USED to sit in this list and no longer do:
  // the code turned out to agree with the design once read (t20's doc defines PASS as zero dropped
  // frames; run_multi_camera passes under 5 ms and fails at 20 ms), so the card was simply wrong.
  const char *type;
  const char *unit;
  // Includes the comparator the design prints ("<= 500"), because dropping it drops the direction.
  const char *value;
};

struct Expectation {
  const char *slug;
  // In approved ORDER. The rendered sequence is compared position by position, not as a set: the
  // same run showed 7 cards with the right rows in the wrong places, because the renderer's
  // allow-list appended every body-recorded row after the parameters instead of carrying the
  // design's position. t16's "Pulse width levels" is row 1 in the design and rendered as row 7.
  std::vector<ExpectedRow> rows;
};

// Transcribed from the approved previews' hardware-trigger cards, in order.
//
// Two previews show cards for more than one trigger scenario and they differ: t03's software card
// omits "Trigger retry interval" (nothing retries without a hardware trigger) and t14's software
// card replaces "Pulse width" with "Trigger source". The hardware-trigger card is the one
// transcribed, matching the run this project measures.
const std::vector<Expectation> &expectations() {
  static const std::vector<Expectation> table = {
      {"t03-pipeline-ready",
       {{"Cycles", "param", "int", "—", "3"},
        {"Buffer count", "param", "int", "—", "2"},
        {"First-frame deadline", "param", "int", "milliseconds", "3000"},
        {"Settle time", "param", "int", "milliseconds", "500"},
        {"Trigger retry interval", "param", "int", "milliseconds", "100"},
        {"Slow-start guard", "param", "int", "milliseconds", "2000"},
        {"PASS threshold", "threshold", "int", "milliseconds", "≤ 500"},
        {"WARN threshold", "threshold", "int", "milliseconds", "≤ 1500"},
        {"Backend memory", "param", "string", "—", "MMAP"}}},
      {"t04-no-streamon",
       {{"Buffer count", "param", "int", "—", "2"},
        {"Poll timeout", "param", "int", "milliseconds", "50"},
        {"Backend memory", "param", "string", "—", "MMAP"}}},
      {"t05-pollerr-handling",
       {{"Baseline captures", "param", "int", "—", "3"},
        {"Recovery captures", "param", "int", "—", "3"},
        {"Warmup count", "param", "int", "—", "3"},
        {"Minimum recovery", "threshold", "int", "—", "≥ 2"},
        {"Poll timeout", "param", "int", "milliseconds", "100"},
        {"Backend memory", "param", "string", "—", "MMAP"}}},
      {"t06-stream-cycles",
       {{"Full cycles", "param", "int", "—", "20"},
        {"Rapid cycles", "param", "int", "—", "50"},
        {"Full warmup", "param", "int", "—", "3"},
        {"Rapid warmup", "param", "int", "—", "2"},
        {"Full timeout", "param", "int", "milliseconds", "150"},
        {"Rapid timeout", "param", "int", "milliseconds", "200"},
        {"Rapid pacing", "param", "int", "milliseconds", "250"},
        {"Slow-start guard", "param", "int", "milliseconds", "2000"},
        {"Full pass threshold", "threshold", "int", "—", "≤ 0"},
        {"Full warn threshold", "threshold", "int", "—", "≤ 2"},
        {"Rapid pass threshold", "threshold", "float", "percent", "≥ 90"},
        {"Rapid warn threshold", "threshold", "float", "percent", "≥ 70"},
        {"Backend memory", "param", "string", "—", "MMAP"}}},
      {"t07-multi-buffer",
       {{"Sample count", "param", "int", "—", "20"},
        {"Max buffers", "param", "int", "—", "5"},
        {"Warmup count", "param", "int", "—", "3"},
        {"Capture timeout", "param", "int", "milliseconds", "100"},
        {"Sample interval", "param", "int", "milliseconds", "200"},
        {"Backend memory", "param", "string", "—", "MMAP"}}},
      {"t08-buffer-overwrite",
       {{"Buffer count", "param", "int", "—", "2"},
        {"Variant A triggers", "param", "int", "—", "100"},
        {"Variant A interval", "param", "int", "milliseconds", "100"},
        {"Variant B triggers", "param", "int", "—", "200"},
        {"Variant B interval", "param", "int", "milliseconds", "50"},
        {"Settle time", "param", "int", "milliseconds", "500"},
        {"Max error flags", "threshold", "int", "—", "0"},
        {"Backend memory", "param", "string", "—", "MMAP"}}},
      {"t09-buffer-recycling",
       {{"Reps per delay", "param", "int", "—", "10"},
        {"Capture timeout", "param", "int", "milliseconds", "100"},
        {"Inter-rep interval", "param", "int", "milliseconds", "100"},
        {"Warmup count", "param", "int", "—", "5"},
        {"Min safe cliff delay", "threshold", "int", "milliseconds", "50"},
        {"Backend memory", "param", "string", "—", "MMAP"}}},
      {"t10-buffer-flags",
       {{"Sample count", "param", "int", "—", "50"},
        {"Capture timeout", "param", "int", "milliseconds", "100"},
        {"Sample interval", "param", "int", "milliseconds", "100"},
        {"Warmup count", "param", "int", "—", "5"},
        {"Max error flags", "threshold", "int", "—", "0"},
        {"Backend memory", "param", "string", "—", "MMAP"}}},
      {"t11-memory-throughput",
       {{"Backend memory", "param", "string", "—", "MMAP"},
        {"Allocated buffers", "param", "int", "—", "2"},
        {"Minimum repetitions", "param", "int", "—", "100"},
        {"Target sample time", "param", "int", "milliseconds", "100"},
        {"Timer", "fixed", "string", "—", "Monotonic"},
        {"Stream state", "derived", "string", "—", "Not started"}}},
      {"t12-dmabuf-cache-sync",
       {{"Requested samples", "param", "int", "—", "20"},
        {"Compared data", "fixed", "string", "—", "Full bytesused"},
        {"Warmup frames", "param", "int", "—", "5"},
        {"Capture timeout", "param", "int", "milliseconds", "100"},
        {"Buffer count", "param", "int", "—", "2"}}},
      {"t13-poll-timeout-cliff",
       {{"Probe samples per timeout", "param", "int", "—", "10"},
        {"Stability rounds", "param", "int", "—", "5"},
        {"Frames per round", "param", "int", "—", "10"},
        {"Warmup frames", "param", "int", "—", "10"},
        {"Configured safe margin", "threshold", "int", "milliseconds", "5"},
        {"Production timeout", "param", "int", "milliseconds", "100"},
        {"Backend memory", "param", "string", "—", "MMAP"}}},
      {"t14-trigger-latency",
       {{"Latency samples", "param", "int", "—", "50"},
        {"Warmup triggers", "param", "int", "—", "5"},
        {"Capture timeout", "param", "int", "milliseconds", "100"},
        {"Sample interval", "param", "int", "milliseconds", "200"},
        {"Pulse width", "param", "int", "milliseconds", "13"},
        {"Backend memory", "param", "string", "—", "MMAP"}}},
      {"t15-nonblock-vs-block",
       {{"Samples per mode", "param", "int", "—", "30"},
        {"Spin deadline", "param", "int", "milliseconds", "100"},
        {"Sample interval", "param", "int", "milliseconds", "200"},
        {"Warmup frames", "param", "int", "—", "5"},
        {"Backend memory", "param", "string", "—", "MMAP"}}},
      {"t16-gpio-pulse-width",
       {{"Pulse width levels", "param", "int", "—", "11"},
        {"Samples per width", "param", "int", "—", "8"},
        {"Total captures", "derived", "int", "—", "88"},
        {"Poll timeout", "param", "int", "milliseconds", "500"},
        {"Warmup frames", "param", "int", "—", "5"},
        {"LOW edge reference", "derived", "string", "—", "HIGH + width"},
        {"Trigger edge", "derived", "string", "—", "Rising"},
        {"Backend memory", "param", "string", "—", "MMAP"}}},
      {"t17-format-comparison",
       {{"Samples per format", "param", "int", "—", "20"},
        {"Memcpy repetitions", "param", "int", "—", "50"},
        {"Sizeimage", "derived", "float", "mebibytes", "4.69"},
        {"Capture timeout", "param", "int", "milliseconds", "500"},
        {"Latency basis", "derived", "string", "—", "Frame availability"},
        {"Throughput divisor", "fixed", "string", "—", "Binary MiB"},
        {"Backend memory", "param", "string", "—", "MMAP"}}},
      {"t18-control-sweep",
       {{"Controls discovered", "derived", "int", "—", "15"},
        {"Writable controls", "derived", "int", "—", "13"},
        {"Captures per value", "param", "int", "—", "20"},
        {"Capture timeout", "param", "int", "milliseconds", "200"},
        {"Practical impact threshold", "threshold", "float", "milliseconds", "≥ 1.0"},
        {"Measured max difference", "derived", "float", "milliseconds", "0.005"},
        {"Backend memory", "param", "string", "—", "MMAP"}}},
      {"t19-resolution-sweep",
       {{"Samples per resolution", "param", "int", "—", "15"},
        {"Resolutions enumerated", "derived", "int", "—", "1"},
        {"Pixel format", "derived", "string", "—", "YUYV"},
        {"Capture timeout", "param", "int", "milliseconds", "500"},
        {"Latency basis", "derived", "string", "—", "Frame availability"},
        {"Backend memory", "param", "string", "—", "MMAP"}}},
      {"t20-sequence-continuity",
       {{"Requested frames", "param", "int", "—", "100"},
        {"Warmup frames", "param", "int", "—", "5"},
        {"Capture timeout", "param", "int", "milliseconds", "100"},
        {"Max allowed gaps", "threshold", "int", "—", "0"},
        {"Continuity signal", "fixed", "string", "—", "buffer.sequence"},
        {"Backend memory", "param", "string", "—", "MMAP"}}},
      {"t21-timestamp-monotonicity",
       {{"Requested frames", "param", "int", "—", "100"},
        {"Warmup frames", "param", "int", "—", "5"},
        {"Capture timeout", "param", "int", "milliseconds", "100"},
        {"Max non-monotonic events", "threshold", "int", "—", "0"},
        {"Ordering signal", "fixed", "string", "—", "buffer.timestamp"},
        {"Backend memory", "param", "string", "—", "MMAP"}}},
      {"t22-stuck-frame",
       {{"Frames requested", "param", "int", "—", "50"},
        {"Pairs compared", "derived", "int", "—", "49"},
        {"Compare bytes", "param", "int", "bytes", "4096"},
        {"Identical threshold", "threshold", "int", "—", "≥ 5"},
        {"Capture timeout", "param", "int", "milliseconds", "100"},
        {"Backend memory", "param", "string", "—", "MMAP"}}},
      {"t23-sustained-capture",
       {{"Test duration", "param", "int", "seconds", "60"},
        {"Window size", "param", "int", "seconds", "10"},
        {"Sample interval", "param", "int", "milliseconds", "100"},
        {"Capture timeout", "param", "int", "milliseconds", "100"},
        {"Warmup frames", "param", "int", "—", "5"},
        {"Drift PASS limit", "threshold", "float", "milliseconds", "≤ 1.0"},
        {"Backend memory", "param", "string", "—", "MMAP"}}},
      {"t24-latency-under-load",
       {{"Samples per phase", "param", "int", "—", "30"},
        {"Load threads", "param", "int", "—", "4"},
        {"Baseline timeout", "param", "int", "milliseconds", "100"},
        {"Load phase timeout", "param", "int", "milliseconds", "200"},
        {"P95 delta PASS limit", "threshold", "float", "milliseconds", "≤ 5.0"},
        {"P95 delta FAIL limit", "threshold", "float", "milliseconds", "≥ 20.0"},
        {"Backend memory", "param", "string", "—", "MMAP"}}},
      {"t25-multi-camera",
       {{"Requested rounds", "param", "int", "—", "50"},
        {"Participants", "derived", "int", "—", "4"},
        {"Round deadline", "param", "int", "milliseconds", "200"},
        {"Capture PASS limit", "threshold", "float", "percent", "100"},
        {"Capture FAIL limit", "threshold", "float", "percent", "< 95"},
        {"Sync PASS limit", "threshold", "float", "milliseconds", "< 5.0"},
        {"Sync FAIL limit", "threshold", "float", "milliseconds", "≥ 20.0"},
        {"Backend memory", "param", "string", "—", "MMAP"}}},
      {"t26-cold-start",
       {{"Fresh cycles", "param", "int", "—", "10"},
        {"Observation window (frames)", "param", "int", "—", "30"},
        {"Reference window", "param", "int", "—", "5"},
        {"Latency tolerance", "threshold", "float", "percent", "≤ 15"},
        {"Capture timeout", "param", "int", "milliseconds", "500"},
        {"Backend memory", "param", "string", "—", "MMAP"}}},
  };
  return table;
}

// The `derived` rows each test BODY records via record_derived_config(), replayed here.
//
// The bodies need a real V4L2 device, so this test cannot run them -- it records the same labels
// with representative values, which is enough to prove the renderer places them in Test
// Configuration under a `derived` source. That the BODY actually calls record_derived_config is
// checked separately, by scanning the runner source (see the loop over kDerivedCallSites below).
struct DerivedRow {
  const char *slug;
  const char *label;
  const char *value;
  const char *unit;
};

const std::vector<DerivedRow> &derived_rows() {
  static const std::vector<DerivedRow> table = {
      {"t16-gpio-pulse-width", "Pulse width levels", "11", ""},
      {"t16-gpio-pulse-width", "Total captures", "88", ""},
      {"t17-format-comparison", "Sizeimage", "4.69", "mebibytes"},
      {"t18-control-sweep", "Controls discovered", "15", ""},
      {"t18-control-sweep", "Writable controls", "13", ""},
      {"t18-control-sweep", "Measured max difference", "0.005", "milliseconds"},
      {"t19-resolution-sweep", "Resolutions enumerated", "1", ""},
      {"t19-resolution-sweep", "Pixel format", "YUYV", ""},
      {"t22-stuck-frame", "Pairs compared", "49", ""},
      {"t25-multi-camera", "Participants", "4", ""},
  };
  return table;
}

// Every runner function that must call record_derived_config, with the label it records. A body
// that stops recording would leave the row missing on a device run while this test, which replays
// the call itself, stayed green -- the vacuous shape this project has hit three times.
struct DerivedCallSite {
  const char *function;
  const char *label;
};

const std::vector<DerivedCallSite> &kDerivedCallSites() {
  static const std::vector<DerivedCallSite> table = {
      {"run_gpio_pulse_width", "Total captures"},
      {"run_gpio_pulse_width", "Pulse width levels"},
      {"run_format_comparison", "Sizeimage"},
      {"run_control_sweep", "Controls discovered"},
      {"run_control_sweep", "Writable controls"},
      {"run_control_sweep", "Measured max difference"},
      {"run_resolution_sweep", "Resolutions enumerated"},
      {"run_resolution_sweep", "Pixel format"},
      {"run_stuck_frame", "Pairs compared"},
      {"run_multi_camera", "Participants"},
  };
  return table;
}

std::string read_file(const std::string &path) {
  std::ifstream input(path);
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

// The Variable and Source cells of every Test Configuration row, in render order.
//
// Bounded to the config section's own </section>. Scanning to the end of the card would pick up
// grid rows from the measurement tables that follow, so a missing configuration row could be
// "found" in a later section and the sequence check would pass on the wrong evidence.
// Cell text as a reader sees it. t25's "< 5.0" is correctly emitted as "&lt; 5.0", so comparing raw
// markup would fail on a row that renders exactly right.
std::string unescaped(const std::string &raw) {
  static const std::pair<const char *, const char *> kEntities[] = {
      {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&#39;", "'"}, {"&amp;", "&"}};
  std::string text = raw;
  for (const auto &entity : kEntities) {
    const std::string from = entity.first;
    const std::string to = entity.second;
    for (std::size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size())) {
      text.replace(at, from.size(), to);
    }
  }
  return text;
}

std::vector<std::vector<std::string>> config_cells(const std::string &html) {
  std::vector<std::vector<std::string>> rows;
  const std::size_t at = html.find("config-section");
  if (at == std::string::npos) {
    return rows;
  }
  const std::size_t close = html.find("</section>", at);
  const std::string section = html.substr(at, close == std::string::npos ? std::string::npos : close - at);
  const std::string open = "<div class=\"grid-row";
  for (std::size_t r = section.find(open); r != std::string::npos; r = section.find(open, r + 1)) {
    const std::size_t row_end = section.find("</div>", r);
    std::vector<std::string> cells;
    for (std::size_t c = section.find("<span>", r); c != std::string::npos && c < row_end;
         c = section.find("<span>", c + 1)) {
      const std::size_t end = section.find("</span>", c);
      if (end == std::string::npos) {
        break;
      }
      cells.push_back(unescaped(section.substr(c + 6, end - c - 6)));
    }
    if (cells.size() >= 5) {
      rows.push_back(cells);
    }
  }
  return rows;
}

std::vector<std::pair<std::string, std::string>> config_rows(const std::string &html) {
  std::vector<std::pair<std::string, std::string>> rows;
  const std::size_t at = html.find("config-section");
  if (at == std::string::npos) {
    return rows;
  }
  const std::size_t close = html.find("</section>", at);
  const std::string section = html.substr(at, close == std::string::npos ? std::string::npos : close - at);
  const std::string open = "<div class=\"grid-row";
  for (std::size_t r = section.find(open); r != std::string::npos; r = section.find(open, r + 1)) {
    std::vector<std::string> cells;
    for (std::size_t c = section.find("<span>", r); cells.size() < 2 && c != std::string::npos;
         c = section.find("<span>", c + 1)) {
      const std::size_t end = section.find("</span>", c);
      if (end == std::string::npos) {
        break;
      }
      cells.push_back(section.substr(c + 6, end - c - 6));
    }
    if (cells.size() == 2) {
      rows.emplace_back(cells[0], cells[1]);
    }
  }
  return rows;
}

// The Variable cell of every row in the Test Configuration section, in render order.
std::vector<std::string> config_labels(const std::string &html) {
  std::vector<std::string> labels;
  for (const auto &row : config_rows(html)) {
    labels.push_back(row.first);
  }
  return labels;
}

}  // namespace

int main() {
  std::size_t expected_total = 0;
  std::size_t present_total = 0;

  for (const auto &e : expectations()) {
    // The runner is what records configuration, so the test result here carries only what the
    // runner would have produced -- nothing is hand-fed. `record_run_parameters` is the seam:
    // it is the function the runners call, exposed so this test observes the same data a device
    // run writes.
    v4l2diag::TestResult test;
    test.id = e.slug;
    test.name = e.slug;
    test.status = v4l2diag::TestStatus::Pass;
    test.memory_backend = "mmap";
    test.duration_ms = 1000;
    test.category = "capture";
    test.summary = "probe";
    v4l2diag::record_run_parameters(&test);
    // ...plus the rows this test's BODY records once its measurement exists.
    for (const auto &d : derived_rows()) {
      if (test.id == d.slug) {
        v4l2diag::record_derived_config(&test, d.label, d.value, d.unit);
      }
    }

    const std::string html = v4l2diag::render_test_content(test);
    const std::vector<std::pair<std::string, std::string>> got_rows = config_rows(html);
    const std::vector<std::string> got = config_labels(html);
    const std::set<std::string> got_set(got.begin(), got.end());

    std::vector<std::string> missing;
    for (const ExpectedRow &row : e.rows) {
      ++expected_total;
      if (got_set.count(row.label) != 0) {
        ++present_total;
      } else {
        missing.push_back(row.label);
      }
    }
    // Nine rows across six tests carry `Source: derived` in the approved previews: they are
    // computed DURING the run, not read from a parameter table -- T18's "Controls discovered",
    // T19's "Pixel format", T16's "Total captures", T17's "Sizeimage", T22's "Pairs compared",
    // T18's "Measured max difference", T25's "Participants". record_run_parameters() cannot know
    // them, and the test bodies that can need a real V4L2 device, so they are recorded there and
    // tracked here. Listing them explicitly means the count cannot grow silently: a NEW missing
    // row fails, and a derived row that starts rendering must leave this list.
    std::string joined;
    for (const auto &m : missing) {
      joined += (joined.empty() ? "" : ", ") + m;
    }
    check(missing.empty(), std::string(e.slug) + " is missing " + std::to_string(missing.size()) + " of " +
                               std::to_string(e.rows.size()) + " approved configuration rows: " + joined);

    // ...in the approved ORDER. Position is part of the design: t18's card opens with what the run
    // discovered ("Controls discovered", "Writable controls") before the settings that drove it, and
    // t11's opens with the backend. Comparing sets cannot see that, and did not: the 02:24 run put
    // every body-recorded row at the bottom of 7 cards and no assertion moved.
    if (missing.empty()) {
      std::vector<std::string> approved_order;
      for (const ExpectedRow &row : e.rows) {
        approved_order.push_back(row.label);
      }
      std::string diff;
      for (std::size_t i = 0; i < approved_order.size() && i < got.size(); ++i) {
        if (approved_order[i] != got[i]) {
          diff = " first difference at row " + std::to_string(i + 1) + ": expected '" + approved_order[i] + "', got '" +
                 got[i] + "'";
          break;
        }
      }
      check(approved_order == got,
            std::string(e.slug) + " renders its configuration rows out of the approved order." + diff);
    }

    // ...and each row's Source cell must be the one the approved card shows. Derived and fixed rows
    // make a claim about PROVENANCE -- a computed measurement or a constant of the method, not a
    // setting somebody chose -- and rendering them all as `param` states the opposite.
    for (const ExpectedRow &row : e.rows) {
      const auto at =
          std::find_if(got_rows.begin(), got_rows.end(),
                       [&row](const std::pair<std::string, std::string> &r) { return r.first == row.label; });
      if (at == got_rows.end()) {
        continue;  // already reported as missing
      }
      check(at->second == row.source, std::string(e.slug) + " renders '" + row.label + "' with source '" + at->second +
                                          "'; the approved card shows '" + row.source + "'");
    }

    // ...and the Type, Unit and Value cells alongside it. A threshold that prints "500" where the
    // design states "<= 500" has dropped the direction of the comparison, which is the only thing
    // that tells a ceiling from a floor.
    for (const ExpectedRow &row : e.rows) {
      const auto cells = config_cells(html);
      const auto at = std::find_if(cells.begin(), cells.end(),
                                   [&row](const std::vector<std::string> &c) { return c[0] == row.label; });
      if (at == cells.end()) {
        continue;  // already reported as missing
      }
      const std::string got = (*at)[2] + "|" + (*at)[3] + "|" + (*at)[4];
      const std::string want = std::string(row.type) + "|" + row.unit + "|" + row.value;
      check(got == want, std::string(e.slug) + " renders '" + row.label + "' as type|unit|value " + got +
                             "; the approved card shows " + want);
    }

    // The honest empty state must be GONE for every test that has approved rows: it told the
    // reader nothing was configured while the runner held the values all along.
    check(std::find(got.begin(), got.end(), "Unavailable") == got.end(),
          std::string(e.slug) + " still renders the \"No parameters were recorded\" empty state");
  }

  // --- exactly the approved rows, no extras, no duplicates ----------------------
  //
  // The 2026-08-12 device run showed T13 with 25 rows where the approved card specifies 7: the
  // runner's own measurement lines ("coarse: 150ms -> 10/10", "stability round 1: ...") reached the
  // table, and three settings appeared TWICE -- once from record_run_parameters() and once from a
  // pre-existing push_back in the body. Counting only the missing rows could not see either fault.
  for (const auto &e : expectations()) {
    v4l2diag::TestResult test;
    test.id = e.slug;
    test.name = e.slug;
    test.status = v4l2diag::TestStatus::Pass;
    test.memory_backend = "mmap";
    test.duration_ms = 1000;
    test.category = "capture";
    test.summary = "probe";
    v4l2diag::record_run_parameters(&test);
    for (const auto &d : derived_rows()) {
      if (test.id == d.slug) {
        v4l2diag::record_derived_config(&test, d.label, d.value, d.unit);
      }
    }
    const std::vector<std::string> got = config_labels(v4l2diag::render_test_content(test));

    // No duplicates: a row stating a setting twice invites the reader to wonder which one ran.
    std::set<std::string> seen;
    std::vector<std::string> dupes;
    for (const auto &g : got) {
      if (!seen.insert(g).second) {
        dupes.push_back(g);
      }
    }
    std::string dupe_list;
    for (const auto &d : dupes) {
      dupe_list += (dupe_list.empty() ? "" : ", ") + d;
    }
    check(dupes.empty(),
          std::string(e.slug) + " lists " + std::to_string(dupes.size()) + " configuration row(s) twice: " + dupe_list);

    // No extras: every rendered row must be one the approved card names.
    std::vector<std::string> extra;
    for (const auto &g : got) {
      const bool approved =
          std::any_of(e.rows.begin(), e.rows.end(), [&g](const ExpectedRow &row) { return g == row.label; });
      if (!approved) {
        extra.push_back(g);
      }
    }
    std::string extra_list;
    for (const auto &x : extra) {
      extra_list += (extra_list.empty() ? "" : ", ") + x;
    }
    check(extra.empty(), std::string(e.slug) + " lists " + std::to_string(extra.size()) +
                             " row(s) the approved card does not name: " + extra_list);
  }

  // Every lookup row reads from the table that actually holds its key. A row pointed at the wrong
  // table renders 0 -- a number, not an error -- so neither the label check, the order check nor the
  // Source check can see it. Observed: the resolved default tables, not the runner's source text.
  {
    const std::vector<std::string> gaps = v4l2diag::configuration_lookup_gaps();
    std::string joined;
    for (const auto &gap : gaps) {
      joined += "\n    " + gap;
    }
    check(gaps.empty(), std::to_string(gaps.size()) +
                            " configuration row(s) read from a table that does not hold "
                            "their key, so each renders 0:" +
                            joined);
  }

  // Every body that must record a derived row still does. Observed: the call in the runner source,
  // inside the function that owns it.
  {
    const std::string runner = read_file("source/backend/core/src/diagnostic_runner.cpp");
    check(!runner.empty(), "diagnostic_runner.cpp not readable; run from the repository root");
    for (const auto &site : kDerivedCallSites()) {
      const std::size_t body = runner.find(std::string("void ") + site.function + "(");
      if (!check(body != std::string::npos, std::string("runner function ") + site.function + " not found")) {
        continue;
      }
      const std::size_t end = runner.find("\n// Docs:", body);
      const std::string fn = runner.substr(body, end == std::string::npos ? std::string::npos : end - body);
      check(fn.find(std::string("record_derived_config(&r, \"") + site.label + "\"") != std::string::npos ||
                fn.find(std::string("record_derived_config(&r, \"") + site.label) != std::string::npos,
            std::string(site.function) + " no longer records the derived row '" + site.label + "'");
    }
  }

  std::cout << "coverage: " << present_total << " / " << expected_total << " approved configuration rows rendered\n";
  if (failures == 0) {
    std::cout << "test_configuration_contract: PASS\n";
    return 0;
  }
  std::cout << failures << " check(s) failed\n";
  return 1;
}
