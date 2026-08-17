// Parser-level regression guard for the run-request body.
//
// test_selectors used to be parsed by two separate loops, so every selector
// landed in config.test_selectors twice. The select_tests() dedup test cannot
// catch that: it dedups *unmatched*, not the parsed list, and a doubled valid
// selector is invisible there. This checks the parser itself.
#include "v4l2diag/core/run_config_json.hpp"

#include <json/json.h>

#include <algorithm>
#include <iostream>
#include <sstream>
#include <string>

namespace {

bool check(bool condition, const char *what) {
  if (!condition) {
    std::cerr << "FAIL: " << what << "\n";
  }
  return condition;
}

Json::Value parse(const std::string &text) {
  Json::CharReaderBuilder builder;
  Json::Value root;
  std::string errors;
  std::istringstream in(text);
  Json::parseFromStream(builder, in, &root, &errors);
  return root;
}

v4l2diag::RunConfig from(const std::string &text) {
  return v4l2diag::run_config_from_json(parse(text), "/tmp/reports", "/tmp/config", "run-1");
}

int count_of(const std::vector<std::string> &values, const std::string &needle) {
  return static_cast<int>(std::count(values.begin(), values.end(), needle));
}

}  // namespace

int main() {
  bool ok = true;

  // The regression: two entries in, two entries out -- each exactly once.
  {
    const auto config = from(R"({"test_selectors":["stable","t04-no-streamon"]})");
    ok &= check(config.test_selectors.size() == 2,
                "test_selectors was not parsed exactly once per entry");
    ok &= check(count_of(config.test_selectors, "stable") == 1, "'stable' was stored more than once");
    ok &= check(count_of(config.test_selectors, "t04-no-streamon") == 1,
                "'t04-no-streamon' was stored more than once");
    ok &= check(config.test_selectors.front() == "stable", "selector order was not preserved");
  }

  // A selector the caller genuinely repeated is kept as sent: de-duplication is
  // select_tests()' job, not the parser's.
  {
    const auto config = from(R"({"test_selectors":["stable","stable"]})");
    ok &= check(config.test_selectors.size() == 2, "the parser altered a caller-supplied duplicate");
  }

  // An absent or empty list stays empty -- select_tests() reads that as the
  // "stable" set. No "implemented" fallback may reappear here.
  {
    ok &= check(from(R"({})").test_selectors.empty(), "a missing test_selectors produced entries");
    ok &= check(from(R"({"test_selectors":[]})").test_selectors.empty(),
                "an empty test_selectors produced entries");
  }

  // Neighbouring fields must not be double-parsed either.
  {
    // report_formats is still in the body on purpose: v5 ignores it (plan 2.10), and
    // a legacy request must not disturb the fields around it.
    const auto config = from(R"({"memory_backends":["mmap","dmabuf"],
                                 "report_formats":["json","html"],
                                 "slaves":[{"path":"/dev/video2"},{"path":"/dev/video4"}]})");
    ok &= check(config.memory_backends.size() == 2, "memory_backends was not parsed exactly once per entry");
    ok &= check(config.slaves.size() == 2, "slaves was not parsed exactly once per entry");
  }

  // Documented defaults.
  {
    const auto config = from(R"({})");
    ok &= check(config.memory_backends.size() == 1 && config.memory_backends.front() == v4l2diag::MemoryBackend::Mmap,
                "the default memory backend changed");
    ok &= check(config.threshold_config_id == "default", "the default threshold config id changed");
    ok &= check(config.output_directory == "/tmp/reports/web-run-run-1", "the output directory layout changed");
    ok &= check(config.config_directory == "/tmp/config", "the config directory was not propagated");
  }

  return ok ? 0 : 1;
}
