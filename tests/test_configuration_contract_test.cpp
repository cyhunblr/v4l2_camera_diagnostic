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
       {{"Cycles", "param"},
        {"Buffer count", "param"},
        {"First-frame deadline", "param"},
        {"Settle time", "param"},
        {"Trigger retry interval", "param"},
        {"Slow-start guard", "param"},
        {"PASS threshold", "threshold"},
        {"WARN threshold", "threshold"},
        {"Backend memory", "param"}}},
      {"t04-no-streamon", {{"Buffer count", "param"}, {"Poll timeout", "param"}, {"Backend memory", "param"}}},
      {"t05-pollerr-handling",
       {{"Baseline captures", "param"},
        {"Recovery captures", "param"},
        {"Warmup count", "param"},
        {"Minimum recovery", "threshold"},
        {"Poll timeout", "param"},
        {"Backend memory", "param"}}},
      {"t06-stream-cycles",
       {{"Full cycles", "param"},
        {"Rapid cycles", "param"},
        {"Full warmup", "param"},
        {"Rapid warmup", "param"},
        {"Full timeout", "param"},
        {"Rapid timeout", "param"},
        {"Rapid pacing", "param"},
        {"Slow-start guard", "param"},
        {"Full pass threshold", "threshold"},
        {"Full warn threshold", "threshold"},
        {"Rapid pass threshold", "threshold"},
        {"Rapid warn threshold", "threshold"},
        {"Backend memory", "param"}}},
      {"t07-multi-buffer",
       {{"Sample count", "param"},
        {"Max buffers", "param"},
        {"Warmup count", "param"},
        {"Capture timeout", "param"},
        {"Sample interval", "param"},
        {"Backend memory", "param"}}},
      {"t08-buffer-overwrite",
       {{"Buffer count", "param"},
        {"Variant A triggers", "param"},
        {"Variant A interval", "param"},
        {"Variant B triggers", "param"},
        {"Variant B interval", "param"},
        {"Settle time", "param"},
        {"Max error flags", "threshold"},
        {"Backend memory", "param"}}},
      {"t09-buffer-recycling",
       {{"Reps per delay", "param"},
        {"Capture timeout", "param"},
        {"Inter-rep interval", "param"},
        {"Warmup count", "param"},
        {"Min safe cliff delay", "threshold"},
        {"Backend memory", "param"}}},
      {"t10-buffer-flags",
       {{"Sample count", "param"},
        {"Capture timeout", "param"},
        {"Sample interval", "param"},
        {"Warmup count", "param"},
        {"Max error flags", "threshold"},
        {"Backend memory", "param"}}},
      {"t11-memory-throughput",
       {{"Backend memory", "param"},
        {"Allocated buffers", "param"},
        {"Minimum repetitions", "param"},
        {"Target sample time", "param"},
        {"Timer", "fixed"},
        {"Stream state", "derived"}}},
      {"t12-dmabuf-cache-sync",
       {{"Requested samples", "param"},
        {"Compared data", "fixed"},
        {"Warmup frames", "param"},
        {"Capture timeout", "param"},
        {"Buffer count", "param"}}},
      {"t13-poll-timeout-cliff",
       {{"Probe samples per timeout", "param"},
        {"Stability rounds", "param"},
        {"Frames per round", "param"},
        {"Warmup frames", "param"},
        {"Configured safe margin", "threshold"},
        {"Production timeout", "param"},
        {"Backend memory", "param"}}},
      {"t14-trigger-latency",
       {{"Latency samples", "param"},
        {"Warmup triggers", "param"},
        {"Capture timeout", "param"},
        {"Sample interval", "param"},
        {"Pulse width", "param"},
        {"Backend memory", "param"}}},
      {"t15-nonblock-vs-block",
       {{"Samples per mode", "param"},
        {"Spin deadline", "param"},
        {"Sample interval", "param"},
        {"Warmup frames", "param"},
        {"Backend memory", "param"}}},
      {"t16-gpio-pulse-width",
       {{"Pulse width levels", "param"},
        {"Samples per width", "param"},
        {"Total captures", "derived"},
        {"Poll timeout", "param"},
        {"Warmup frames", "param"},
        {"LOW edge reference", "derived"},
        {"Trigger edge", "derived"},
        {"Backend memory", "param"}}},
      {"t17-format-comparison",
       {{"Samples per format", "param"},
        {"Memcpy repetitions", "param"},
        {"Sizeimage", "derived"},
        {"Capture timeout", "param"},
        {"Latency basis", "derived"},
        {"Throughput divisor", "fixed"},
        {"Backend memory", "param"}}},
      {"t18-control-sweep",
       {{"Controls discovered", "derived"},
        {"Writable controls", "derived"},
        {"Captures per value", "param"},
        {"Capture timeout", "param"},
        {"Practical impact threshold", "threshold"},
        {"Measured max difference", "derived"},
        {"Backend memory", "param"}}},
      {"t19-resolution-sweep",
       {{"Samples per resolution", "param"},
        {"Resolutions enumerated", "derived"},
        {"Pixel format", "derived"},
        {"Capture timeout", "param"},
        {"Latency basis", "derived"},
        {"Backend memory", "param"}}},
      {"t20-sequence-continuity",
       {{"Requested frames", "param"},
        {"Warmup frames", "param"},
        {"Capture timeout", "param"},
        {"Max allowed gaps", "threshold"},
        {"Continuity signal", "fixed"},
        {"Backend memory", "param"}}},
      {"t21-timestamp-monotonicity",
       {{"Requested frames", "param"},
        {"Warmup frames", "param"},
        {"Capture timeout", "param"},
        {"Max non-monotonic events", "threshold"},
        {"Ordering signal", "fixed"},
        {"Backend memory", "param"}}},
      {"t22-stuck-frame",
       {{"Frames requested", "param"},
        {"Pairs compared", "derived"},
        {"Compare bytes", "param"},
        {"Identical threshold", "threshold"},
        {"Capture timeout", "param"},
        {"Backend memory", "param"}}},
      {"t23-sustained-capture",
       {{"Test duration", "param"},
        {"Window size", "param"},
        {"Sample interval", "param"},
        {"Capture timeout", "param"},
        {"Warmup frames", "param"},
        {"Drift PASS limit", "threshold"},
        {"Backend memory", "param"}}},
      {"t24-latency-under-load",
       {{"Samples per phase", "param"},
        {"Load threads", "param"},
        {"Baseline timeout", "param"},
        {"Load phase timeout", "param"},
        {"P95 delta PASS limit", "threshold"},
        {"P95 delta FAIL limit", "threshold"},
        {"Backend memory", "param"}}},
      {"t25-multi-camera",
       {{"Requested rounds", "param"},
        {"Participants", "derived"},
        {"Round deadline", "param"},
        {"Capture PASS limit", "threshold"},
        {"Capture FAIL limit", "threshold"},
        {"Sync PASS limit", "threshold"},
        {"Sync FAIL limit", "threshold"},
        {"Backend memory", "param"}}},
      {"t26-cold-start",
       {{"Fresh cycles", "param"},
        {"Observation window (frames)", "param"},
        {"Reference window", "param"},
        {"Latency tolerance", "threshold"},
        {"Capture timeout", "param"},
        {"Backend memory", "param"}}},
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
      {"t19-resolution-sweep", "Pixel format", "UYVY", ""},
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
