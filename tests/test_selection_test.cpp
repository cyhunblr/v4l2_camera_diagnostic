#include "v4l2diag/core/test_registry.hpp"

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool contains(const std::vector<v4l2diag::TestDefinition> &tests, const std::string &id) {
  return std::any_of(tests.begin(), tests.end(), [&](const v4l2diag::TestDefinition &t) { return t.id == id; });
}

// Ids used below, with the tags that make them interesting here:
//   t05-pollerr-handling  stress
//   t08-buffer-overwrite  stress
//   t23-sustained-capture long-running
//   t04-no-streamon       stable
bool check(bool condition, const char *what) {
  if (!condition) {
    std::cerr << "FAIL: " << what << "\n";
  }
  return condition;
}

}  // namespace

int main() {
  bool ok = true;

  // Default selection: returns stable tests.
  {
    const auto sel = v4l2diag::select_tests({});
    ok &= check(!contains(sel, "t05-pollerr-handling"), "stress test leaked into default selection");
    ok &= check(!contains(sel, "t23-sustained-capture"), "long-running test leaked into default selection");
    ok &= check(contains(sel, "t04-no-streamon"), "stable test missing from default selection");
    ok &= check(!contains(sel, "t08-buffer-overwrite"), "stress test must not be in default");
    ok &= check(contains(sel, "t03-pipeline-ready"), "t03-pipeline-ready must run by default");
    ok &= check(!contains(sel, "t06-stream-cycles"), "t06 must not run by default");
  }

  // Tags as selectors.
  {
    const auto sel = v4l2diag::select_tests({"stress"});
    ok &= check(contains(sel, "t05-pollerr-handling"), "stress tag did not admit the stress test");
    ok &= check(!contains(sel, "t23-sustained-capture"), "stress tag wrongly admitted a long-running test");
  }
  {
    const auto sel = v4l2diag::select_tests({"long-running"});
    ok &= check(contains(sel, "t23-sustained-capture"), "long-running tag did not admit the long-running test");
    ok &= check(!contains(sel, "t05-pollerr-handling"), "long-running tag wrongly admitted a stress test");
  }

  // An exact id is an explicit opt-in.
  {
    const auto sel = v4l2diag::select_tests({"t05-pollerr-handling"});
    ok &= check(sel.size() == 1 && contains(sel, "t05-pollerr-handling"), "explicit id must run even when stress");
  }
  {
    const auto sel = v4l2diag::select_tests({"t23-sustained-capture"});
    ok &=
        check(sel.size() == 1 && contains(sel, "t23-sustained-capture"), "explicit id must run even when long-running");
  }

  // Category selectors.
  {
    const auto sel = v4l2diag::select_tests({"stream-state"});
    ok &= check(contains(sel, "t04-no-streamon"), "category selector lost a stable test");
    ok &= check(contains(sel, "t05-pollerr-handling"), "category selector must admit all in category");
  }
  {
    const auto sel = v4l2diag::select_tests({"all"});
    ok &= check(contains(sel, "t05-pollerr-handling"), "'all' must admit stress test");
  }

  // Unknown selectors match nothing; duplicates collapse.
  {
    ok &= check(v4l2diag::select_tests({"t99-nope"}).empty(), "unknown selector matched something");
    const auto sel = v4l2diag::select_tests({"t04-no-streamon", "t04-no-streamon", "stream-state"});
    ok &= check(std::count_if(sel.begin(), sel.end(),
                              [](const v4l2diag::TestDefinition &t) { return t.id == "t04-no-streamon"; }) == 1,
                "duplicate selectors produced duplicate entries");
  }

  // An empty selector list means "the stable set", not "nothing". The web
  // server used to substitute the selector "implemented" here -- a tag that
  // has never existed -- so select_tests() matched nothing and a run started
  // with zero tests.
  {
    const auto empty = v4l2diag::select_tests({});
    ok &= check(!empty.empty(), "an empty selector list selected no tests");
    ok &= check(contains(empty, "t04-no-streamon"), "the empty-selector default lost a stable test");
    ok &= check(!contains(empty, "t05-pollerr-handling"), "the empty-selector default admitted a stress test");

    // "implemented" is not a tag, category or id: it must match nothing, which
    // is exactly why it was the wrong thing to fall back to.
    ok &= check(v4l2diag::select_tests({"implemented"}).empty(), "'implemented' unexpectedly matched tests");
  }

  // Unmatched selectors must be reportable so the caller can warn instead of
  // silently running a different set than the user asked for.
  {
    std::vector<std::string> unmatched;
    const auto sel = v4l2diag::select_tests({"t04-no-streamon", "implemented", "t99-nope"}, &unmatched);
    ok &= check(contains(sel, "t04-no-streamon"), "a valid selector was dropped alongside invalid ones");
    ok &= check(unmatched.size() == 2, "unmatched selectors were not all reported");
    ok &= check(std::find(unmatched.begin(), unmatched.end(), "implemented") != unmatched.end(),
                "'implemented' was not reported as unmatched");
    ok &= check(std::find(unmatched.begin(), unmatched.end(), "t99-nope") != unmatched.end(),
                "'t99-nope' was not reported as unmatched");

    std::vector<std::string> none;
    v4l2diag::select_tests({"stable"}, &none);
    ok &= check(none.empty(), "a matching selector was reported as unmatched");

    std::vector<std::string> empty_in;
    v4l2diag::select_tests({}, &empty_in);
    ok &= check(empty_in.empty(), "the empty selector list reported an unmatched entry");

    // A repeated unknown selector must warn once, not once per occurrence.
    std::vector<std::string> repeated;
    v4l2diag::select_tests({"t99-nope", "t99-nope", "t99-nope"}, &repeated);
    ok &= check(repeated.size() == 1, "a repeated unknown selector was reported more than once");
    ok &= check(repeated.front() == "t99-nope", "the repeated unknown selector was reported wrongly");
  }

  return ok ? 0 : 1;
}
