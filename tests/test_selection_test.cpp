#include "v4l2diag/core/test_registry.hpp"

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool contains(const std::vector<v4l2diag::TestDefinition> &tests, const std::string &id) {
  return std::any_of(tests.begin(), tests.end(), [&](const v4l2diag::TestDefinition &t) { return t.id == id; });
}

// Ids used below, with the flags that make them interesting here:
//   t04-pollerr-handling  experimental + risky
//   t07-buffer-overwrite  risky only (must NOT be gated)
//   t22-sustained-capture long-running
//   t03-no-streamon       plain, and shares the stream-state category with t04
bool check(bool condition, const char *what) {
  if (!condition) {
    std::cerr << "FAIL: " << what << "\n";
  }
  return condition;
}

}  // namespace

int main() {
  bool ok = true;

  // Default selection: gates apply.
  {
    const auto sel = v4l2diag::select_tests({}, /*long=*/false, /*experimental=*/false);
    ok &= check(!contains(sel, "t04-pollerr-handling"), "experimental test leaked into default selection");
    ok &= check(!contains(sel, "t22-sustained-capture"), "long-running test leaked into default selection");
    ok &= check(contains(sel, "t03-no-streamon"), "plain test missing from default selection");
    ok &= check(contains(sel, "t07-buffer-overwrite"), "risky-only test must not be gated");
    // t05 cycles STREAMON hard enough to reset a board whose sensors share a
    // deserializer, so it must stay opt-in.
    ok &= check(!contains(sel, "t05-stream-cycles"), "t05 must not run by default");
  }

  // Opting in lifts the respective gate.
  {
    const auto sel = v4l2diag::select_tests({}, /*long=*/false, /*experimental=*/true);
    ok &= check(contains(sel, "t04-pollerr-handling"), "include_experimental did not admit the experimental test");
    ok &= check(!contains(sel, "t22-sustained-capture"), "include_experimental wrongly admitted a long-running test");
  }
  {
    const auto sel = v4l2diag::select_tests({}, /*long=*/true, /*experimental=*/false);
    ok &= check(contains(sel, "t22-sustained-capture"), "include_long did not admit the long-running test");
    ok &= check(!contains(sel, "t04-pollerr-handling"), "include_long wrongly admitted an experimental test");
  }

  // An exact id is an explicit opt-in and overrides both gates.
  {
    const auto sel = v4l2diag::select_tests({"t04-pollerr-handling"}, false, false);
    ok &=
        check(sel.size() == 1 && contains(sel, "t04-pollerr-handling"), "explicit id must run even when experimental");
  }
  {
    const auto sel = v4l2diag::select_tests({"t22-sustained-capture"}, false, false);
    ok &=
        check(sel.size() == 1 && contains(sel, "t22-sustained-capture"), "explicit id must run even when long-running");
  }

  // Group selectors are broad requests, so they keep the gates.
  {
    const auto sel = v4l2diag::select_tests({"stream-state"}, false, false);
    ok &= check(contains(sel, "t03-no-streamon"), "category selector lost a plain test");
    ok &= check(!contains(sel, "t04-pollerr-handling"), "category selector must not bypass the experimental gate");
  }
  {
    const auto sel = v4l2diag::select_tests({"all"}, false, false);
    ok &= check(!contains(sel, "t04-pollerr-handling"), "'all' must not bypass the experimental gate");
  }

  // Unknown selectors match nothing; duplicates collapse.
  {
    ok &= check(v4l2diag::select_tests({"t99-nope"}, false, false).empty(), "unknown selector matched something");
    const auto sel = v4l2diag::select_tests({"t03-no-streamon", "t03-no-streamon", "stream-state"}, false, false);
    ok &= check(std::count_if(sel.begin(), sel.end(),
                              [](const v4l2diag::TestDefinition &t) { return t.id == "t03-no-streamon"; }) == 1,
                "duplicate selectors produced duplicate entries");
  }

  return ok ? 0 : 1;
}
