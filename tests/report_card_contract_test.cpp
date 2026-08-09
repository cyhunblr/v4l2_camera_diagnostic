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
  static const char *kNumeric[] = {"1920x1280_latency_mean",
                                   "1920x1280_latency_p95",
                                   "1920x1280_throughput_mbps",
                                   "allocated_buffers",
                                   "avg_eagain_spins",
                                   "backend_dmabuf",
                                   "backend_mmap",
                                   "backend_userptr",
                                   "baseline_captures",
                                   "baseline_frames",
                                   "baseline_latency_jitter",
                                   "baseline_latency_max",
                                   "baseline_latency_mean",
                                   "baseline_latency_min",
                                   "baseline_latency_p95",
                                   "baseline_latency_stddev",
                                   "baseline_ok",
                                   "block_captures",
                                   "block_latency_jitter",
                                   "block_latency_max",
                                   "block_latency_mean",
                                   "block_latency_min",
                                   "block_latency_p95",
                                   "block_latency_stddev",
                                   "block_mean_ms",
                                   "block_p95_ms",
                                   "cameras",
                                   "captured",
                                   "captures",
                                   "censored_cycles",
                                   "cliff_ms",
                                   "control_count",
                                   "cycles",
                                   "cycles_completed",
                                   "delta_jitter",
                                   "delta_max",
                                   "delta_mean",
                                   "delta_mean_ms",
                                   "delta_min",
                                   "delta_p95",
                                   "delta_p95_ms",
                                   "delta_stddev",
                                   "dqbuf_errno",
                                   "dqbuf_failed",
                                   "dropped_frames",
                                   "duplicates",
                                   "eagain_spins_to_first_frame",
                                   "enumerated",
                                   "error_flag_total",
                                   "first_frame_latency_max",
                                   "first_frame_latency_mean",
                                   "first_frame_max_ms",
                                   "first_frame_mean_ms",
                                   "first_frame_ms_max",
                                   "first_frame_ms_mean",
                                   "first_frame_timeouts",
                                   "first_miss_ms",
                                   "flag_eof",
                                   "flag_error",
                                   "flag_keyframe",
                                   "flag_soe",
                                   "flag_ts_copy",
                                   "flag_ts_monotonic",
                                   "format_count",
                                   "formats_tested",
                                   "frame_interval_ms",
                                   "frame_rate_hz",
                                   "frame_size_bytes",
                                   "frames",
                                   "frames_available_A",
                                   "frames_available_B",
                                   "frames_captured",
                                   "frames_missed",
                                   "frames_tested",
                                   "full_aborted",
                                   "full_configured",
                                   "full_cycle_failures",
                                   "full_cycles",
                                   "full_cycles_attempted",
                                   "full_cycles_success",
                                   "full_frame_mib_s",
                                   "full_start_fail",
                                   "gaps",
                                   "granted_for_1",
                                   "granted_for_2",
                                   "granted_for_3",
                                   "granted_for_4",
                                   "granted_for_5",
                                   "hits_10ms",
                                   "hits_13",
                                   "hits_13ms",
                                   "hits_15ms",
                                   "hits_1ms",
                                   "hits_20",
                                   "hits_20ms",
                                   "hits_25ms",
                                   "hits_2ms",
                                   "hits_30ms",
                                   "hits_3ms",
                                   "hits_5",
                                   "hits_5ms",
                                   "hits_7ms",
                                   "identical_pairs",
                                   "idle_p95_ms",
                                   "interval_jitter_ms",
                                   "isx021_found",
                                   "lat_high_avg_10ms",
                                   "lat_high_avg_13ms",
                                   "lat_high_avg_15ms",
                                   "lat_high_avg_1ms",
                                   "lat_high_avg_20ms",
                                   "lat_high_avg_25ms",
                                   "lat_high_avg_2ms",
                                   "lat_high_avg_30ms",
                                   "lat_high_avg_3ms",
                                   "lat_high_avg_5ms",
                                   "lat_high_avg_7ms",
                                   "lat_low_avg_10ms",
                                   "lat_low_avg_13ms",
                                   "lat_low_avg_15ms",
                                   "lat_low_avg_1ms",
                                   "lat_low_avg_20ms",
                                   "lat_low_avg_25ms",
                                   "lat_low_avg_2ms",
                                   "lat_low_avg_30ms",
                                   "lat_low_avg_3ms",
                                   "lat_low_avg_5ms",
                                   "lat_low_avg_7ms",
                                   "latency_drift_ms",
                                   "latency_jitter",
                                   "latency_max",
                                   "latency_max_ms",
                                   "latency_mean",
                                   "latency_mean_ms",
                                   "latency_min",
                                   "latency_p95",
                                   "latency_stddev",
                                   "latency_stddev_ms",
                                   "ll0_bp0_wi0_mean_ms",
                                   "ll0_bp0_wi1_mean_ms",
                                   "ll0_bp1_wi0_mean_ms",
                                   "ll1_bp0_wi0_mean_ms",
                                   "ll1_bp0_wi1_mean_ms",
                                   "ll1_bp1_wi1_mean_ms",
                                   "load_captures",
                                   "load_latency_jitter",
                                   "load_latency_max",
                                   "load_latency_mean",
                                   "load_latency_min",
                                   "load_latency_p95",
                                   "load_latency_stddev",
                                   "load_p95_ms",
                                   "mapped_capacity_bytes",
                                   "max_consecutive_miss",
                                   "max_gap",
                                   "max_identical_run",
                                   "measured",
                                   "min_reliable_width_ms",
                                   "mismatches",
                                   "mmap_4k_mbps",
                                   "mmap_64k_mbps",
                                   "mmap_full_mbps",
                                   "non_monotonic",
                                   "nonblock_captures",
                                   "nonblock_latency_jitter",
                                   "nonblock_latency_max",
                                   "nonblock_latency_mean",
                                   "nonblock_latency_min",
                                   "nonblock_latency_p95",
                                   "nonblock_latency_stddev",
                                   "nonblock_mean_ms",
                                   "nonblock_p95_ms",
                                   "nv16_latency_max",
                                   "nv16_latency_mean",
                                   "nv16_throughput_mbps",
                                   "out_of_range_count",
                                   "poll_returned",
                                   "pollerr_events",
                                   "pollerr_raised",
                                   "pollhup_raised",
                                   "range_h",
                                   "range_l",
                                   "rapid_aborted",
                                   "rapid_capture_timeouts",
                                   "rapid_configured",
                                   "rapid_cycles",
                                   "rapid_cycles_attempted",
                                   "rapid_cycles_ok",
                                   "rapid_cycles_total",
                                   "rapid_skipped",
                                   "rapid_start_fail",
                                   "rapid_start_failures",
                                   "recovery_frames",
                                   "recovery_ok",
                                   "regressions",
                                   "requested",
                                   "res_1920x1080_mean_ms",
                                   "resolution_count",
                                   "restreamon_ok",
                                   "safety_margin_ms",
                                   "selected_backend_supported",
                                   "sessions",
                                   "sizeimage_bytes",
                                   "spread_h",
                                   "spread_l",
                                   "stability",
                                   "stability_confirmed",
                                   "stability_rounds_passed",
                                   "start_failures_total",
                                   "streamon_attempts_max",
                                   "streamon_max_ms",
                                   "streamon_mean_ms",
                                   "streamon_ms_max",
                                   "streamon_ms_mean",
                                   "stuck",
                                   "success_rate_pct",
                                   "supports_capture",
                                   "supports_streaming",
                                   "sync_max_ms",
                                   "timeouts",
                                   "trigger_fire_spread_max",
                                   "trigger_fire_spread_mean",
                                   "trigger_pulses_to_first_frame",
                                   "triggers_A",
                                   "triggers_B",
                                   "ts_non_monotonic",
                                   "uyvy_coverage",
                                   "uyvy_latency_max",
                                   "uyvy_latency_mean",
                                   "uyvy_mean_ms",
                                   "uyvy_throughput_mbps",
                                   "wall_buf_offset_jitter",
                                   "wall_buf_offset_max",
                                   "wall_buf_offset_mean",
                                   "wall_buf_offset_min",
                                   "wall_buf_offset_p95",
                                   "wall_buf_offset_stddev",
                                   "warmup_max_frames",
                                   "warmup_mean_frames",
                                   "writable_count",
                                   "yuyv_coverage",
                                   "yuyv_mean_ms"};
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
  // T26 reads its per-cycle warm-up counts from these lines, in the runner's own wording
  // (diagnostic_runner.cpp writes "cycle N: warmup=M frames"). Without them the approved
  // column chart has nothing to draw.
  test.details.push_back("cycle 1: warmup=1 frames");
  test.details.push_back("cycle 2: warmup=2 frames");
  test.details.push_back("cycle 3: warmup=1 frames");
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

  // --- each section role appears at most once per card, in the approved order ---
  // Measured on the 2026-08-08 device report: T15, T16 and T17 printed "Test Configuration"
  // TWICE -- once from a kv_section() call that pre-dated the canonical roles, once from
  // test_configuration(). The vocabulary check above passes either way, because both
  // labels are legal words; only counting them per card catches the duplicate. T03 also
  // carried an extra "Timing by Cycle" section that the approved preview shows as an item
  // label inside Measurement, not as a section of its own.
  {
    static const std::vector<std::string> kOrder = {"Measurement", "Measurement Result", "Test Configuration"};
    std::size_t at = 0;
    while ((at = cards.find("<article class=\"test-card", at)) != std::string::npos) {
      const std::size_t end = cards.find("</article>", at);
      const std::string card = cards.substr(at, end == std::string::npos ? std::string::npos : end - at);
      const std::size_t id_at = card.find("id=\"result-mmap-");
      std::string slug;
      if (id_at != std::string::npos) {
        const std::size_t start = id_at + 16;
        slug = card.substr(start, card.find('"', start) - start);
      }
      std::vector<std::string> seen;
      for (const std::string &label : tag_texts(card, "<h3 class=\"section-label\"")) {
        if (std::find(kOrder.begin(), kOrder.end(), label) != kOrder.end()) {
          seen.push_back(label);
        }
      }
      for (const std::string &role : kOrder) {
        const std::size_t times = static_cast<std::size_t>(std::count(seen.begin(), seen.end(), role));
        check(times <= 1, slug + " opens the \"" + role + "\" section " + std::to_string(times) +
                              " times; each role appears once per card");
      }
      // Relative order of the roles that are present: inputs are stated last, after the
      // measurement and its verdict. Compared as rank indices rather than by sorting the
      // labels, because a duplicated role makes a sort comparison meaningless.
      std::string trail;
      bool ascending = true;
      std::size_t previous_rank = 0;
      for (const std::string &label : seen) {
        const std::size_t rank =
            static_cast<std::size_t>(std::find(kOrder.begin(), kOrder.end(), label) - kOrder.begin());
        if (!trail.empty()) {
          trail += " -> ";
        }
        trail += label;
        if (rank < previous_rank) {
          ascending = false;
        }
        previous_rank = rank;
      }
      check(ascending, slug + " renders its sections out of order: " + trail);
      at += 1;
    }
  }

  // --- an item whose every row is Unavailable is not rendered at all -------
  // A table that says nothing is worse than no table: it occupies the space a reader
  // scans for a finding and answers with four blanks. Measured on the 2026-08-08 device
  // report, T25 (skipped: "requires at least one slave camera") printed an "Aggregate"
  // item whose four rows all read Unavailable, directly under a card that already said
  // no camera participated.
  //
  // The rule is deliberately "every row", not "any row": a partly-filled table still
  // carries a finding, and hiding it would suppress real data (Rule 4c).
  {
    std::size_t at = 0;
    while ((at = cards.find("<h4", at)) != std::string::npos) {
      const std::size_t label_end = cards.find("</h4>", at);
      if (label_end == std::string::npos) {
        break;
      }
      const std::size_t next_item = cards.find("<h4", label_end);
      const std::size_t card_end = cards.find("</article>", label_end);
      const std::size_t stop = std::min(next_item == std::string::npos ? cards.size() : next_item,
                                        card_end == std::string::npos ? cards.size() : card_end);
      const std::string item = cards.substr(label_end, stop - label_end);
      std::size_t rows = 0;
      std::size_t blank = 0;
      std::size_t row_at = 0;
      while ((row_at = item.find("<div class=\"grid-row", row_at)) != std::string::npos) {
        const std::size_t row_end = item.find("</div>", row_at);
        const std::string one =
            item.substr(row_at, row_end == std::string::npos ? std::string::npos : row_end - row_at);
        ++rows;
        if (one.find("Unavailable") != std::string::npos) {
          ++blank;
        }
        row_at += 1;
      }
      if (rows >= 2) {
        const std::string label = tag_texts(cards.substr(at, label_end - at + 5), "<h4").empty()
                                      ? std::string("(unnamed)")
                                      : tag_texts(cards.substr(at, label_end - at + 5), "<h4").front();
        check(blank < rows, "the \"" + label + "\" item renders " + std::to_string(rows) +
                                " rows and every one reads Unavailable; it should not be rendered");
      }
      at = label_end;
    }
  }

  // --- the Test Configuration table uses its own, tighter row rhythm ------
  // Measured against the approved previews: .config-row carries "padding: 5px 16px"
  // while every measurement row carries 6px. The production renderer emitted the same
  // generic .grid-row for both, so the configuration table sat one pixel per row looser
  // than the design. One pixel per row is invisible in a diff and visible in a stack of
  // eight parameters, which is exactly the kind of drift a contract has to hold.
  {
    check(contains(html, ".config-section"),
          "no .config-section rule: the Test Configuration table cannot be styled apart from measurement rows");
    // Observed: the vertical padding override itself. The horizontal 16px is inherited
    // from .grid-row, so only the 5px is restated here.
    check(contains(html, "padding-top: 5px") && contains(html, "padding-bottom: 5px"),
          "the Test Configuration rows do not override the 6px measurement-row padding to 5px");
    // EVERY Test Configuration section must carry the hook, not just one: three cards
    // opened theirs through kv_section(), which emits a <dl class="kv"> and no hook, so
    // those three kept the measurement rhythm and a different table structure entirely.
    std::size_t configs = 0;
    std::size_t hooked = 0;
    const std::string needle = "<h3 class=\"section-label\">Test Configuration</h3>";
    for (std::size_t at = cards.find(needle); at != std::string::npos; at = cards.find(needle, at + 1)) {
      ++configs;
      const std::size_t open_at = cards.rfind("<section", at);
      if (open_at != std::string::npos && cards.compare(open_at, at - open_at, "") != 0) {
        if (cards.substr(open_at, at - open_at).find("config-section") != std::string::npos) {
          ++hooked;
        }
      }
    }
    check(configs > 0, "no Test Configuration section was rendered at all");
    check(hooked == configs, std::to_string(configs - hooked) + " of " + std::to_string(configs) +
                                 " Test Configuration sections lack the config-section class the CSS targets");
  }

  // --- one chart shell, and one name per chart ----------------------------
  // The approved previews put every chart in the same shell: an <h4 class="item-label">
  // names it, an optional .chart-legend explains the marks, and a single .chart-frame
  // wraps them. Measured on the 2026-08-08 device report, production used TWO wrappers
  // (.chart-frame on seven charts, .metric-chart on one) and printed a second heading
  // inside the frame, so one chart carried both "Timing by cycle" (the item label) and
  // "STREAMON and first-frame timing across 3 cycles" (the in-frame title).
  {
    // Two class names wrap charts (.chart-frame from test_content, .metric-chart from
    // report_writer) and both are in use. What has to hold is that they LOOK the same:
    // measured on the device report, .metric-chart carried a border, a white background,
    // a shadow and a -14px pull-out that .chart-frame did not, so an identical chart was
    // boxed or unboxed depending on which function drew it.
    check(count_of(cards, "class=\"chart-frame\"") + count_of(cards, "class=\"metric-chart") > 0,
          "no chart wrapper is present at all");
    check(!contains(html, ".metric-chart { border: 1px solid"),
          ".metric-chart still draws a border box; .chart-frame frames charts with whitespace");
    check(!contains(html, ".metric-chart { margin: 0 -14px; }"),
          ".metric-chart still pulls itself out by 14px, which only made sense with the box padding");
    check(count_of(html, ".metric-chart { padding: 14px 0; }") == 1,
          "the .metric-chart padding rule is declared more than once");
    // A chart is named once. Most charts carry their name in the in-frame title and have
    // no item label; two carried BOTH, so the reader saw "Timing by cycle" immediately
    // above "STREAMON and first-frame timing across 3 cycles". The duplicate is what is
    // forbidden -- not the in-frame title, which for eleven charts is the only name there
    // is and removing it would delete the description (Rule 4c).
    std::size_t doubled = 0;
    const std::string label_open = "<h4 class=\"item-label\">";
    for (std::size_t at = cards.find(label_open); at != std::string::npos; at = cards.find(label_open, at + 1)) {
      const std::size_t label_end = cards.find("</h4>", at);
      if (label_end == std::string::npos) {
        break;
      }
      // Only the markup that IMMEDIATELY follows the label counts. Scanning to the next
      // label swept up whatever came later in the section, so a plain table item was
      // reported as double-named because a chart appeared further down.
      const std::string after = cards.substr(label_end + 5, 60);
      const std::size_t frame_at = after.find("chart-frame");
      const std::size_t metric_at = after.find("metric-chart");
      if (frame_at == std::string::npos && metric_at == std::string::npos) {
        continue;  // not a chart item
      }
      const std::size_t chart_start = label_end + 5 +
                                      std::min(frame_at == std::string::npos ? after.size() : frame_at,
                                               metric_at == std::string::npos ? after.size() : metric_at);
      if (cards.compare(chart_start, 200, "") != 0 &&
          cards.substr(chart_start, 200).find("metric-chart-title") != std::string::npos) {
        ++doubled;
      }
    }
    check(doubled == 0,
          std::to_string(doubled) + " chart(s) carry an item label AND an in-frame title; a chart is named once");
  }

  // --- a chart never names an axis the test does not sweep ----------------
  // The automatic chart selector groups metrics that share a prefix and then names the
  // category axis from a spec table -- but when no spec matched it fell back to the
  // literal "Format". Measured on the 2026-08-08 device report, T24 (CPU load impact,
  // whose prefixes are baseline_ and load_) rendered a chart titled "Capture latency by
  // format" and an axis captioned "Format". T24 sweeps no pixel format at all, so the
  // reader was told the run compared something it never looked at.
  {
    static const std::vector<std::string> kFormatSweepers = {"t17-format-comparison"};
    std::size_t at = 0;
    while ((at = cards.find("<article class=\"test-card", at)) != std::string::npos) {
      const std::size_t end = cards.find("</article>", at);
      const std::string card = cards.substr(at, end == std::string::npos ? std::string::npos : end - at);
      const std::size_t id_at = card.find("id=\"result-mmap-");
      std::string slug;
      if (id_at != std::string::npos) {
        const std::size_t start = id_at + 16;
        slug = card.substr(start, card.find('"', start) - start);
      }
      const bool sweeps_format =
          std::find(kFormatSweepers.begin(), kFormatSweepers.end(), slug) != kFormatSweepers.end();
      if (!slug.empty() && !sweeps_format) {
        check(card.find("by format<") == std::string::npos && card.find(">Format</text>") == std::string::npos,
              slug + " renders a chart against a \"Format\" axis, but it sweeps no pixel format");
      }
      at += 1;
    }
  }

  // --- every class the bar charts emit is actually styled -----------------
  // Emitting .bar-track without a rule for it renders an unstyled chart: the bars have no
  // height, no background and no colour, so the numbers stack as plain text. Measured
  // before this check: all fifteen classes the bar renderer emits were undefined.
  {
    static const std::vector<std::string> kBarClasses = {
        ".bar-row", ".bar-label", ".bar-track", ".bar-fill", ".bar-info", ".bar-axis", ".scale-in", ".legend-mean",
        ".legend-max", ".legend-nonblock", ".legend-block", ".legend-ok", ".legend-miss", ".legend-p95",
        // T26's SVG column chart draws with these.
        ".col-stab", ".svg-label", ".svg-value", ".axis-title", ".gridline", ".legend-stab"};
    for (const std::string &name : kBarClasses) {
      check(contains(html, name + " ") || contains(html, name + ",") || contains(html, name + "{"),
            "the bar charts emit " + name + " but no CSS rule defines it");
    }
  }

  // --- the six approved bar charts are rendered ---------------------------
  // The approved previews give these tests a horizontal bar chart drawn from metrics the
  // runner records. test_charts_approved() deliberately keeps the GENERIC selector off
  // them (an automatic dot chart matched none of the previews), so each is drawn by its
  // own renderer instead -- this check is what keeps "deliberately off" from quietly
  // becoming "missing".
  //
  // Observed per card: the item label that names the chart, the .chart-frame shell, and
  // at least one .bar-row inside it.
  {
    static const std::vector<std::pair<std::string, std::string>> kApprovedCharts = {
        {"t14-trigger-latency", "Latency distribution"},
        {"t15-nonblock-vs-block", "Latency by capture mode"},
        {"t17-format-comparison", "Capture latency by pixel format"},
        {"t19-resolution-sweep", "Capture performance"},
        {"t20-sequence-continuity", "Sequence continuity"},
        {"t21-timestamp-monotonicity", "Sampled buffer timestamp delta"},
    };
    // T26 draws an SVG column chart rather than CSS bars, so it is checked separately
    // below; it was already ON the approved list yet rendered nothing, because its
    // metrics are named "warmup_mean_frames" and the generic selector only groups the
    // _mean/_p95/_max suffixes.
    for (const auto &entry : kApprovedCharts) {
      std::size_t at = 0;
      std::string card;
      while ((at = cards.find("id=\"result-mmap-" + entry.first, at)) != std::string::npos) {
        const std::size_t start = cards.rfind("<article class=\"test-card", at);
        const std::size_t end = cards.find("</article>", at);
        card = cards.substr(start, end == std::string::npos ? std::string::npos : end - start);
        break;
      }
      if (card.empty()) {
        check(false, entry.first + " has no card at all");
        continue;
      }
      const bool named = card.find(entry.second) != std::string::npos;
      check(named, entry.first + " does not name its approved chart \"" + entry.second + "\"");
      if (!named) {
        continue;
      }
      const std::size_t label_at = card.find(entry.second);
      const std::string after = card.substr(label_at, 900);
      check(after.find("chart-frame") != std::string::npos,
            entry.first + ": the \"" + entry.second + "\" chart is not in a .chart-frame");
      check(after.find("bar-row") != std::string::npos,
            entry.first + ": the \"" + entry.second + "\" chart draws no bars");
    }
  }

  // --- T26's approved column chart --------------------------------------
  // Observed: the item label, the frame, and one <rect> per cycle the run recorded. The
  // per-cycle warm-up counts live in the detail lines ("cycle 1: warmup=1 frames"), not
  // in a metric, which is why no automatic selector could ever have drawn this.
  {
    std::size_t at = cards.find("id=\"result-mmap-t26-cold-start");
    if (at == std::string::npos) {
      check(false, "t26-cold-start has no card at all");
    } else {
      const std::size_t start = cards.rfind("<article class=\"test-card", at);
      const std::size_t end = cards.find("</article>", at);
      const std::string card = cards.substr(start, end == std::string::npos ? std::string::npos : end - start);
      const std::size_t label_at = card.find("Warm-up outcome by fresh session");
      check(label_at != std::string::npos, "t26-cold-start does not name its approved warm-up chart");
      if (label_at != std::string::npos) {
        const std::string after = card.substr(label_at, 1200);
        check(after.find("chart-frame") != std::string::npos, "t26's warm-up chart is not in a .chart-frame");
        check(after.find("<rect") != std::string::npos, "t26's warm-up chart draws no columns");
      }
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
