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

struct Expectation {
  const char *slug;
  std::vector<const char *> labels;
};

// Transcribed from the approved previews' hardware-trigger cards.
const std::vector<Expectation> &expectations() {
  static const std::vector<Expectation> table = {
      {"t03-pipeline-ready",
       {"Cycles", "Buffer count", "First-frame deadline", "Settle time", "Trigger retry interval", "Slow-start guard",
        "PASS threshold", "WARN threshold", "Backend memory"}},
      {"t04-no-streamon", {"Buffer count", "Poll timeout", "Backend memory"}},
      {"t05-pollerr-handling",
       {"Baseline captures", "Recovery captures", "Warmup count", "Minimum recovery", "Poll timeout",
        "Backend memory"}},
      {"t06-stream-cycles",
       {"Full cycles", "Rapid cycles", "Full warmup", "Rapid warmup", "Full timeout", "Rapid timeout", "Rapid pacing",
        "Slow-start guard", "Full pass threshold", "Full warn threshold", "Rapid pass threshold",
        "Rapid warn threshold", "Backend memory"}},
      {"t07-multi-buffer",
       {"Sample count", "Max buffers", "Warmup count", "Capture timeout", "Sample interval", "Backend memory"}},
      {"t08-buffer-overwrite",
       {"Buffer count", "Variant A triggers", "Variant A interval", "Variant B triggers", "Variant B interval",
        "Settle time", "Max error flags", "Backend memory"}},
      {"t10-buffer-flags",
       {"Sample count", "Capture timeout", "Sample interval", "Warmup count", "Max error flags", "Backend memory"}},
      {"t11-memory-throughput",
       {"Backend memory", "Allocated buffers", "Minimum repetitions", "Target sample time", "Timer", "Stream state"}},
      {"t12-dmabuf-cache-sync",
       {"Requested samples", "Compared data", "Warmup frames", "Capture timeout", "Buffer count"}},
      {"t13-poll-timeout-cliff",
       {"Probe samples per timeout", "Stability rounds", "Frames per round", "Warmup frames", "Configured safe margin",
        "Production timeout", "Backend memory"}},
      {"t14-trigger-latency",
       {"Latency samples", "Warmup triggers", "Capture timeout", "Sample interval", "Pulse width", "Backend memory"}},
      {"t15-nonblock-vs-block",
       {"Samples per mode", "Spin deadline", "Sample interval", "Warmup frames", "Backend memory"}},
      {"t16-gpio-pulse-width",
       {"Pulse width levels", "Samples per width", "Total captures", "Poll timeout", "Warmup frames",
        "LOW edge reference", "Trigger edge", "Backend memory"}},
      {"t17-format-comparison",
       {"Samples per format", "Memcpy repetitions", "Sizeimage", "Capture timeout", "Latency basis",
        "Throughput divisor", "Backend memory"}},
      {"t18-control-sweep",
       {"Controls discovered", "Writable controls", "Captures per value", "Capture timeout",
        "Practical impact threshold", "Measured max difference", "Backend memory"}},
      {"t19-resolution-sweep",
       {"Samples per resolution", "Resolutions enumerated", "Pixel format", "Capture timeout", "Latency basis",
        "Backend memory"}},
      {"t20-sequence-continuity",
       {"Requested frames", "Warmup frames", "Capture timeout", "Max allowed gaps", "Continuity signal",
        "Backend memory"}},
      {"t21-timestamp-monotonicity",
       {"Requested frames", "Warmup frames", "Capture timeout", "Max non-monotonic events", "Ordering signal",
        "Backend memory"}},
      {"t22-stuck-frame",
       {"Frames requested", "Pairs compared", "Compare bytes", "Identical threshold", "Capture timeout",
        "Backend memory"}},
      {"t23-sustained-capture",
       {"Test duration", "Window size", "Sample interval", "Capture timeout", "Warmup frames", "Drift PASS limit",
        "Backend memory"}},
      {"t24-latency-under-load",
       {"Samples per phase", "Load threads", "Baseline timeout", "Load phase timeout", "P95 delta PASS limit",
        "P95 delta FAIL limit", "Backend memory"}},
      {"t25-multi-camera",
       {"Requested rounds", "Participants", "Round deadline", "Capture PASS limit", "Capture FAIL limit",
        "Sync PASS limit", "Sync FAIL limit", "Backend memory"}},
      {"t26-cold-start",
       {"Fresh cycles", "Observation window (frames)", "Reference window", "Latency tolerance", "Capture timeout",
        "Backend memory"}},
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

// The Source cell of the Test Configuration row whose Variable cell is `label`.
std::string source_of(const std::string &html, const std::string &label) {
  const std::size_t cfg = html.find("Test Configuration");
  if (cfg == std::string::npos) {
    return "(no section)";
  }
  const std::string section = html.substr(cfg);
  const std::string needle = "<span>" + label + "</span>";
  const std::size_t at = section.find(needle);
  if (at == std::string::npos) {
    return "(not rendered)";
  }
  const std::size_t open = section.find("<span>", at + needle.size());
  if (open == std::string::npos) {
    return "(row truncated)";
  }
  const std::size_t end = section.find("</span>", open);
  return end == std::string::npos ? "(row truncated)" : section.substr(open + 6, end - open - 6);
}

// The Variable cell of every row in the Test Configuration section, in render order.
std::vector<std::string> config_labels(const std::string &html) {
  std::vector<std::string> labels;
  const std::size_t at = html.find("Test Configuration");
  if (at == std::string::npos) {
    return labels;
  }
  const std::string section = html.substr(at);
  const std::string open = "<div class=\"grid-row";
  for (std::size_t r = section.find(open); r != std::string::npos; r = section.find(open, r + 1)) {
    const std::size_t first = section.find("<span>", r);
    if (first == std::string::npos) {
      break;
    }
    const std::size_t end = section.find("</span>", first);
    if (end == std::string::npos) {
      break;
    }
    labels.push_back(section.substr(first + 6, end - first - 6));
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
    const std::vector<std::string> got = config_labels(html);
    const std::set<std::string> got_set(got.begin(), got.end());

    std::vector<std::string> missing;
    for (const char *label : e.labels) {
      ++expected_total;
      if (got_set.count(label) != 0) {
        ++present_total;
      } else {
        missing.push_back(label);
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
                               std::to_string(e.labels.size()) + " approved configuration rows: " + joined);

    // ...and each derived row must be STAMPED `derived`, not `param`. Checking only the label let a
    // sabotage that disabled the derived source kind pass: the rows rendered, mislabelled as
    // settings, which is the claim the Source column exists to make.
    for (const auto &d : derived_rows()) {
      if (test.id != d.slug) {
        continue;
      }
      const std::string cell = source_of(html, d.label);
      check(cell == "derived", std::string(e.slug) + " renders '" + d.label + "' with source '" + cell +
                                   "'; a computed value must read 'derived'");
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
          std::any_of(e.labels.begin(), e.labels.end(), [&g](const char *label) { return g == label; });
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
