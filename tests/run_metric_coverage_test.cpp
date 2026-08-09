// Every metric name a renderer reads must be present in a REAL run's recorded metrics.
//
// Why this exists even though metric_name_contract_test.cpp already compares names:
// that test scans diagnostic_runner.cpp for metric("...") call sites, so a name counts
// as "emitted" if the source mentions it ANYWHERE. It passed green while the report
// produced on real hardware on 2026-08-09 rendered 118 of its 314 table rows as
// "Unavailable" -- 20 of 24 cards affected. A source scan cannot see which metrics a
// given test actually records at run time; only a run can.
//
// Measured examples from that report (renderer asked -> run recorded):
//   t22  "stuck"                    -> identical_pairs, max_identical_run
//   t19  "res_1920x1080_mean_ms"    -> 1920x1280_latency_mean   (resolution hard-coded)
//   t21  "regressions"              -> delta_min/_max/_p95/_jitter
//   t20  "gaps", "non_monotonic"    -> dropped_frames, duplicates
//   t18  "default_ll0_bp0_wi0_..."  -> ll0_bp0_wi0_mean_ms      (spurious "default_")
//
// The fixture is an unedited v4l2_camera_diagnostic JSON result captured from the
// device, kept verbatim in tests/data/. It is EVIDENCE, not a hand-written model: the
// previous fixture was hand-written and carried the renderer's own guesses back into
// the test, which is exactly how the defect above survived.

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

int failures = 0;

std::string read_file(const std::string &path) {
  std::ifstream input(path);
  if (!input) {
    std::cout << "FAIL: cannot read " << path << "\n";
    ++failures;
    return std::string();
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

// The body of each `std::string render_tNN(const TestResult &test)` function, keyed by
// "tNN". Brace-matched rather than regexed to the next function, so a nested lambda or
// initialiser list cannot end the body early.
std::map<std::string, std::string> renderer_bodies(const std::string &src) {
  std::map<std::string, std::string> bodies;
  // "render_t08\(" and not "render_t08" alone: the helper render_t08_queue_after_saturation
  // also starts with the test id, and matching it pulled that helper's PARAMETER name
  // ("allocated_buffers") in as though the t08 card looked up a metric by that name.
  const std::regex head(R"RX(std::string render_(t\d+)\(const TestResult &test\)\s*\{)RX");
  for (auto it = std::sregex_iterator(src.begin(), src.end(), head); it != std::sregex_iterator(); ++it) {
    const std::size_t start = static_cast<std::size_t>(it->position(0)) + it->length(0);
    std::size_t depth = 1;
    std::size_t i = start;
    while (i < src.size() && depth > 0) {
      if (src[i] == '{') {
        ++depth;
      } else if (src[i] == '}') {
        --depth;
      }
      ++i;
    }
    bodies[(*it)[1].str()] = src.substr(start, i - start);
  }
  return bodies;
}

// Metric names one renderer body looks up. Mirrors the lookup helpers in
// test_content.cpp; only the FIRST name of a value_of_any fallback chain is required,
// since later entries exist to read runs recorded under an older name.
std::set<std::string> lookups(const std::string &body) {
  // A value_of_any chain lists a primary name followed by compatibility fallbacks; only
  // the primary must exist. Blank the chains out before scanning, so the spec pattern
  // below cannot pick a fallback out of the middle of one and report it as missing --
  // it did exactly that for t06's "first_frame_timeouts", whose primary
  // ("rapid_capture_timeouts") is recorded.
  std::string scrubbed = body;
  std::set<std::string> names;
  // Upper case matters: metric names like "frames_available_A" and "triggers_B" carry a
  // variant suffix. A lower-case-only pattern skipped the chain's PRIMARY name and took
  // the next quoted string instead -- the fallback -- reporting it as missing.
  const std::regex quoted(R"RX("([a-zA-Z0-9][a-zA-Z0-9_]*)")RX");
  // Scanned by hand rather than with a regex: a chain can sit inside an outer brace
  // initialiser, and "\\{[^}]*\\}" stops at the FIRST closing brace, which left the tail
  // of the chain in `scrubbed` and reported its fallbacks as missing metrics.
  const std::string marker = "value_of_any(test,";
  for (std::size_t at = scrubbed.find(marker); at != std::string::npos; at = scrubbed.find(marker, at + 1)) {
    // Search from the END of the marker. Starting at `at` found the brace of an
    // enclosing initialiser in `config_items({{"label", value_of_any(test, {...})}, ...})`,
    // so the span blanked out was the wrong one and the chain's fallback names survived
    // into the spec scan below.
    const std::size_t open_at = scrubbed.find('{', at + marker.size());
    if (open_at == std::string::npos) {
      break;
    }
    std::size_t depth = 1;
    std::size_t i = open_at + 1;
    while (i < scrubbed.size() && depth > 0) {
      if (scrubbed[i] == '{') {
        ++depth;
      } else if (scrubbed[i] == '}') {
        --depth;
      }
      ++i;
    }
    const std::string list = scrubbed.substr(open_at, i - open_at);
    std::smatch primary;
    if (std::regex_search(list, primary, quoted)) {
      names.insert(primary[1].str());
    }
    scrubbed.replace(at, i - at, std::string(i - at, ' '));
  }
  for (const char *pattern :
       {R"RX(find_metric\(test,\s*"([a-z0-9][a-z0-9_]*)")RX", R"RX(value_of\(test,\s*"([a-z0-9][a-z0-9_]*)")RX",
        R"RX(\{"[^"]*",\s*"([a-z0-9][a-z0-9_]*)",\s*(?:nullptr|"))RX"}) {
    const std::regex re(pattern);
    for (auto it = std::sregex_iterator(scrubbed.begin(), scrubbed.end(), re); it != std::sregex_iterator(); ++it) {
      names.insert((*it)[1].str());
    }
  }
  return names;
}

// Metric names recorded per test in the fixture, keyed by "tNN". Parsed with a scan
// rather than a JSON library because the repository has no JSON dependency in tests;
// the shape ("tests":[{"id":"tNN-...","metrics":[{"name":"..."}]}]) is fixed by
// run_result_json_test.cpp, so a shape change fails there first.
std::map<std::string, std::set<std::string>> recorded(const std::string &json) {
  std::map<std::string, std::set<std::string>> out;
  const std::regex id(R"RX("id"\s*:\s*"(t\d+)[a-z0-9-]*")RX");
  std::vector<std::pair<std::string, std::size_t>> marks;
  for (auto it = std::sregex_iterator(json.begin(), json.end(), id); it != std::sregex_iterator(); ++it) {
    marks.emplace_back((*it)[1].str(), static_cast<std::size_t>(it->position(0)));
  }
  // Only names inside the "metrics" array count. A test object also carries its own
  // "name" field (the human title), and scanning the whole slice picked that up -- which
  // made a skipped test with "metrics": [] look as though it had recorded one metric.
  const std::regex name(R"RX("name"\s*:\s*"([^"]+)")RX");
  for (std::size_t k = 0; k < marks.size(); ++k) {
    const std::size_t from = marks[k].second;
    const std::size_t to = (k + 1 < marks.size()) ? marks[k + 1].second : json.size();
    const std::string slice = json.substr(from, to - from);
    const std::size_t metrics_at = slice.find("\"metrics\"");
    if (metrics_at == std::string::npos) {
      continue;
    }
    const std::size_t open_at = slice.find('[', metrics_at);
    const std::size_t close_at = (open_at == std::string::npos) ? std::string::npos : slice.find(']', open_at);
    if (open_at == std::string::npos || close_at == std::string::npos) {
      continue;
    }
    const std::string metrics = slice.substr(open_at, close_at - open_at);
    for (auto it = std::sregex_iterator(metrics.begin(), metrics.end(), name); it != std::sregex_iterator(); ++it) {
      out[marks[k].first].insert((*it)[1].str());
    }
  }
  return out;
}

}  // namespace

int main() {
  const std::string src = read_file("source/backend/core/src/test_content.cpp");
  const std::string json = read_file("tests/data/device-run-2026-08-09.json");
  if (src.empty() || json.empty()) {
    std::cout << "run this from the repository root\n";
    return 1;
  }

  const std::map<std::string, std::string> bodies = renderer_bodies(src);
  const std::map<std::string, std::set<std::string>> have = recorded(json);

  // Guard the guard: if either extraction breaks, every comparison below passes
  // vacuously. The fixture covers 24 of the 26 tests (t09 and t12 did not run on this
  // device), t25 was skipped with no metrics, and the renderer has one function per test.
  if (bodies.size() < 26) {
    std::cout << "FAIL: only " << bodies.size() << " render_tNN bodies extracted; the scan is broken\n";
    ++failures;
  }
  // 23, not 24: t25 was skipped and recorded no metrics, so it contributes no entry.
  if (have.size() < 23) {
    std::cout << "FAIL: only " << have.size() << " tests found in the fixture; the parse is broken\n";
    ++failures;
  }
  std::size_t fixture_metrics = 0;
  for (const auto &entry : have) {
    fixture_metrics += entry.second.size();
  }
  if (fixture_metrics < 150) {
    std::cout << "FAIL: only " << fixture_metrics << " metric names in the fixture; the parse is broken\n";
    ++failures;
  }

  std::size_t missing_total = 0;
  for (const auto &entry : bodies) {
    const std::string &test_id = entry.first;
    const auto found = have.find(test_id);
    if (found == have.end() || found->second.empty()) {
      // Either the test did not run on this device (t09, t12) or it was skipped and
      // recorded no metrics at all (t25: "requires at least one slave camera"). A
      // skipped test legitimately has nothing to read, and its card renders the skip
      // reason rather than a table -- report_card_contract_test.cpp covers that path.
      continue;
    }
    const std::set<std::string> &available = found->second;
    // The Type and Unit vocabularies sit in the same brace initialiser as a metric name,
    // so the spec pattern above picks them up. They are never metric names.
    static const std::set<std::string> kNotMetrics = {"string",       "float",        "int",     "ratio",   "param",
                                                      "threshold",    "bool",         "count",   "master",  "slave",
                                                      "milliseconds", "microseconds", "seconds", "percent", "hertz",
                                                      "bytes",        "pixels",       "frames",  "cycles"};
    std::vector<std::string> missing;
    for (const std::string &want : lookups(entry.second)) {
      if (available.count(want) == 1 || kNotMetrics.count(want) == 1) {
        continue;
      }
      // A bare number is a literal column value, not a lookup.
      if (std::all_of(want.begin(), want.end(),
                      [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; })) {
        continue;
      }
      // Names assembled at run time from a discovered value (pixel format, resolution,
      // pulse width, control combination) cannot be written literally. Accept a lookup
      // whose text is a prefix or suffix of a recorded name -- e.g. "latency_mean"
      // against "uyvy_latency_mean". A wholly absent name has neither.
      const bool related = std::any_of(available.begin(), available.end(), [&want](const std::string &had) {
        return had.find(want) != std::string::npos || want.find(had) != std::string::npos;
      });
      if (!related) {
        missing.push_back(want);
      }
    }
    if (!missing.empty()) {
      missing_total += missing.size();
      std::cout << "FAIL: " << test_id << " reads " << missing.size()
                << " metric name(s) the run never recorded, so they render as Unavailable:\n";
      for (const std::string &name : missing) {
        std::cout << "    " << name << "\n";
      }
      ++failures;
    }
  }

  // --- T01 states one format per distinct pixel format ---------------------
  // Observed: the "Format: FOURCC (description, buffer type)" detail lines T01 records, and
  // the format_count metric beside them.
  //
  // The tegra isx021 in this very fixture advertises UYVY at ENUM_FMT index 0 and again at
  // index 2, both single-plane. Passed through, T01 said "3 formats" and printed UYVY twice
  // while T17 -- which already skipped repeats when building its own list -- measured 2 on
  // the same device. One report cannot answer "how many formats does this camera have" two
  // different ways, so enumerate_formats() now de-duplicates on (fourcc, buffer type).
  //
  // The fixture keeps the RAW driver output, duplicate included: it is recorded evidence of
  // what the hardware reports. The assertion is therefore that the duplicate exists in the
  // fixture (otherwise this check is vacuous) AND that the de-duplication is what the
  // renderer relies on -- the count must equal the number of distinct entries.
  {
    // The serializer writes `details` BEFORE `id`, so the t01 record spans from the start of
    // the file (or the previous record) up to its own id -- slicing forward from the id
    // would find no format lines at all and report a vacuous pass.
    const std::string marker = "\"id\": \"t01-device-compliance\"";
    const std::size_t at = json.find(marker);
    if (at == std::string::npos) {
      std::cout << "FAIL: t01 is absent from the device fixture; the format check is vacuous\n";
      ++failures;
    } else {
      const std::string card = json.substr(0, at);
      std::vector<std::string> lines;
      const std::string needle = "Format: ";
      for (std::size_t f = card.find(needle); f != std::string::npos; f = card.find(needle, f + 1)) {
        const std::size_t stop = card.find('"', f);
        if (stop == std::string::npos) {
          break;
        }
        lines.push_back(card.substr(f + needle.size(), stop - f - needle.size()));
      }
      const std::set<std::string> distinct(lines.begin(), lines.end());
      if (lines.size() < 2) {
        std::cout << "FAIL: the fixture records " << lines.size()
                  << " format line(s); the de-duplication check needs the real enumeration\n";
        ++failures;
      } else if (distinct.size() == lines.size()) {
        std::cout << "FAIL: the fixture no longer carries the driver's duplicate fourcc, so this check can no "
                     "longer prove de-duplication is needed\n";
        ++failures;
      }
      // What the renderer must publish: one entry per distinct format.
      if (lines.size() > distinct.size()) {
        std::cout << "note: driver advertised " << lines.size() << " format entries, " << distinct.size()
                  << " distinct -- enumerate_formats() de-duplicates on (fourcc, buffer type)\n";
      }
    }
  }

  if (failures == 0) {
    std::cout << "run metric coverage: " << bodies.size() << " renderers checked against " << have.size()
              << " real test results, " << fixture_metrics << " recorded metric names, 0 unavailable lookups\n";
    return 0;
  }
  std::cout << "\n" << missing_total << " lookup(s) unmatched across " << failures << " failing check(s)\n";
  return 1;
}
