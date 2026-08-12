// The renderer may only ask for metric names the runner actually records.
//
// This is a SOURCE-level check, deliberately. A fixture cannot catch the defect it
// exists to model: a render fixture carried 133 metric names, and
// only 11 of them were names diagnostic_runner.cpp ever emits. Every report rendered
// from that fixture looked complete while the same renderer, fed a real run, would have
// printed "Unavailable" down the column.
//
// Measured examples of the mismatch this locks out:
//   runner emits            renderer looked for
//   latency_max             latency_max_ms
//   nonblock_latency_p95    nonblock_p95_ms
//   baseline_latency_mean   idle_mean_ms
//
// The unit is a separate field on MetricValue, so a "_ms" suffix in the NAME is always
// a guess -- and the three names above differ by more than a suffix anyway.

#include <algorithm>
#include <fstream>
#include <iostream>
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

std::set<std::string> matches(const std::string &text, const std::string &pattern, std::size_t group = 1) {
  std::set<std::string> found;
  const std::regex re(pattern);
  for (auto it = std::sregex_iterator(text.begin(), text.end(), re); it != std::sregex_iterator(); ++it) {
    if ((*it)[group].matched) {
      found.insert((*it)[group].str());
    }
  }
  return found;
}

// Every metric name diagnostic_runner.cpp can emit: the literal metric("name", ...)
// calls, plus the names the push_stats_metrics family builds from a prefix.
std::set<std::string> runner_metric_names(const std::string &runner) {
  std::set<std::string> names = matches(runner, R"RX(metric\("([a-z][a-z0-9_]*)")RX");

  // push_stats_metrics       -> _mean _stddev _min _max _p95 _jitter
  // push_stats_metrics_brief -> _mean _max
  // push_stats_metrics_p95   -> _mean _p95
  const std::vector<std::string> full = {"_mean", "_stddev", "_min", "_max", "_p95", "_jitter"};
  const std::vector<std::string> brief = {"_mean", "_max"};
  const std::vector<std::string> p95 = {"_mean", "_p95"};

  const std::regex call(R"RX(push_stats_metrics(_brief|_p95)?\(\s*r\.metrics,\s*"([a-z][a-z0-9_]*)")RX");
  for (auto it = std::sregex_iterator(runner.begin(), runner.end(), call); it != std::sregex_iterator(); ++it) {
    const std::string kind = (*it)[1].str();
    const std::string prefix = (*it)[2].str();
    const std::vector<std::string> &suffixes = kind == "_brief" ? brief : (kind == "_p95" ? p95 : full);
    for (const std::string &suffix : suffixes) {
      names.insert(prefix + suffix);
    }
  }
  return names;
}

// Every metric name test_content.cpp looks up.
std::set<std::string> renderer_metric_names(const std::string &renderer) {
  std::set<std::string> names;
  // find_metric() is often used as a fallback chain on one line:
  //   find_metric(test, "new") != nullptr ? find_metric(test, "new") : find_metric(test, "old")
  // Only the first name on such a line is required to exist, for the same reason
  // value_of_any's first alternative is.
  {
    std::istringstream lines(renderer);
    std::string line;
    const std::regex fm(R"RX(find_metric\(test,\s*"([a-z][a-z0-9_]{2,})"\))RX");
    while (std::getline(lines, line)) {
      std::smatch first;
      if (std::regex_search(line, first, fm)) {
        names.insert(first[1].str());
      }
    }
  }
  for (const char *pattern : {R"RX(value_of\(test,\s*"([a-z][a-z0-9_]{2,})"\))RX",
                              // VerdictSpec / measurement spec: the primary metric name.
                              R"RX(\{"[^"]*",\s*"([a-z][a-z0-9_]{2,})",\s*(?:nullptr|"))RX"}) {
    const std::set<std::string> found = matches(renderer, pattern);
    names.insert(found.begin(), found.end());
  }
  // value_of_any(test, {"a", "b"}) tries "a" and falls back to "b". Only the FIRST name
  // has to exist: the later ones are compatibility fallbacks for runs recorded under an
  // older name, and requiring them to exist would forbid renaming a metric.
  const std::regex any(R"RX(value_of_any\(test,\s*\{([^}]*)\})RX");
  const std::regex quoted(R"RX("([a-z][a-z0-9_]{2,})")RX");
  for (auto it = std::sregex_iterator(renderer.begin(), renderer.end(), any); it != std::sregex_iterator(); ++it) {
    const std::string list = (*it)[1].str();
    std::smatch first;
    if (std::regex_search(list, first, quoted)) {
      names.insert(first[1].str());
    }
  }
  return names;
}

}  // namespace

int main() {
  const std::string runner = read_file("source/backend/core/src/diagnostic_runner.cpp");
  const std::string renderer = read_file("source/backend/core/src/test_content.cpp");
  if (runner.empty() || renderer.empty()) {
    std::cout << "run this from the repository root\n";
    return 1;
  }

  const std::set<std::string> emitted = runner_metric_names(runner);
  const std::set<std::string> wanted = renderer_metric_names(renderer);

  // Guard the guard: if either extraction breaks, the comparison below passes vacuously.
  if (emitted.size() < 100) {
    std::cout << "FAIL: only " << emitted.size() << " runner metric names extracted; the scan is broken\n";
    ++failures;
  }
  if (wanted.size() < 50) {
    std::cout << "FAIL: only " << wanted.size() << " renderer lookups extracted; the scan is broken\n";
    ++failures;
  }

  // Some metric names are built at RUN TIME from a value the run discovered: the pixel
  // format ("uyvy_latency_mean"), the resolution ("res_1920x1080_mean"), the pulse width
  // ("hits_5ms"), the control combination ("ll0_bp1_wi0_mean"). A static scan cannot
  // enumerate them, so the renderer is allowed to build the same shapes -- the PREFIX is
  // what must match, and each of these is emitted by a metric() call in the runner.
  static const std::vector<std::string> kRuntimePrefixes = {
      "hits_",     "lat_high_avg_", "lat_low_avg_",     "res_", "uyvy_", "yuyv_", "nv16_", "ll0_", "ll1_", "default_ll",
      "triggers_", "granted_for_",  "frames_available_"};

  // The names the renderer asks for that the runner never records. A handful are read
  // from detail lines rather than metrics and are listed explicitly, so that adding a
  // new unmatched name fails instead of quietly joining an open-ended allow list.
  static const std::set<std::string> kFromDetailLines = {"allocated_buffers",
                                                         "backend_memory",
                                                         "baseline_frames",
                                                         "buffers",
                                                         "buffers_requested",
                                                         "cameras",
                                                         "captures",
                                                         "controls",
                                                         "cycles",
                                                         "drops",
                                                         "enumerated",
                                                         "frames",
                                                         "gaps",
                                                         "max_gap",
                                                         "measured",
                                                         "misses",
                                                         "poll_timeout_ms",
                                                         "recovery_frames",
                                                         "regressions",
                                                         "requested",
                                                         "sessions",
                                                         "sizeimage",
                                                         "stuck",
                                                         "warmup_frames",
                                                         "min_recovery_frames",
                                                         "pollerr_events",
                                                         "captured",
                                                         "min_max_spread_ms",
                                                         "jitter_ms",
                                                         "successful_attempts"};

  // Words that are never metric names: the Type vocabulary, the Unit vocabulary, and the
  // role names. The VerdictSpec pattern sees them because they sit in the same brace
  // initialiser as a metric name.
  static const std::set<std::string> kNotMetrics = {"string", "float",        "int",          "ratio",   "master",
                                                    "slave",  "milliseconds", "microseconds", "seconds", "percent",
                                                    "hertz",  "bytes",        "pixels"};

  std::vector<std::string> unmatched;
  for (const std::string &name : wanted) {
    if (kNotMetrics.count(name) == 1) {
      continue;
    }
    if (emitted.count(name) == 1 || kFromDetailLines.count(name) == 1) {
      continue;
    }
    const bool runtime_built = std::any_of(kRuntimePrefixes.begin(), kRuntimePrefixes.end(),
                                           [&name](const std::string &prefix) { return name.rfind(prefix, 0) == 0; });
    if (runtime_built) {
      continue;
    }
    unmatched.push_back(name);
  }

  if (!unmatched.empty()) {
    std::cout << "FAIL: " << unmatched.size() << " metric name(s) the renderer reads are never recorded by the "
              << "runner, so a real run renders them as Unavailable:\n";
    for (const std::string &name : unmatched) {
      std::cout << "  " << name << "\n";
    }
    ++failures;
  }

  if (failures == 0) {
    std::cout << "metric name contract: " << wanted.size() << " renderer lookups, all recorded by the runner\n";
    return 0;
  }
  return 1;
}
