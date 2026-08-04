// The full protected contract of all 26 registered tests: canonical display
// name (authority: docs/web-ui-audit-fix-plan.md §0.5), user-facing layer, and
// the technical category and tags.
//
// The technical keys are stated exactly, not just checked for non-emptiness:
// they drive selector resolution and the "stable" default set, so a category or
// tag changed by accident must fail here rather than silently alter which tests
// a run executes.
#include "v4l2diag/core/test_json.hpp"
#include "v4l2diag/core/test_registry.hpp"

#include <algorithm>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

std::string join(const std::vector<std::string> &values) {
  std::string out;
  for (const auto &value : values) {
    if (!out.empty()) {
      out += ", ";
    }
    out += value;
  }
  return out;
}

bool check(bool condition, const std::string &what) {
  if (!condition) {
    std::cerr << "FAIL: " << what << "\n";
  }
  return condition;
}

struct Expected {
  const char *name;
  v4l2diag::TestLayer layer;
  const char *category;
  std::vector<std::string> tags;
};

// Canonical display name + user-facing layer for every registered test.
const std::map<std::string, Expected> kCanonical = {
    {"t01-device-compliance", {"V4L2 Device Compliance", v4l2diag::TestLayer::Discovery, "discovery", {"stable"}}},
    {"t02-control-inventory", {"V4L2 Control Inventory", v4l2diag::TestLayer::Discovery, "discovery", {"stable"}}},
    {"t03-pipeline-ready", {"Pipeline Readiness after STREAMON", v4l2diag::TestLayer::StateMachine, "stream-state", {"stable"}}},
    {"t04-no-streamon", {"Frame Capture without STREAMON", v4l2diag::TestLayer::StateMachine, "stream-state", {"stable"}}},
    {"t05-pollerr-handling", {"STREAMOFF Error Handling and Recovery", v4l2diag::TestLayer::StateMachine, "stream-state", {"stress"}}},
    {"t06-stream-cycles", {"STREAMON/STREAMOFF Cycle Reliability", v4l2diag::TestLayer::StateMachine, "stream-state", {"stress"}}},
    {"t07-multi-buffer", {"Multi-buffer Configurations", v4l2diag::TestLayer::BufferMemory, "buffering", {"stable"}}},
    {"t08-buffer-overwrite", {"Buffer Saturation Behavior", v4l2diag::TestLayer::BufferMemory, "buffering", {"stress"}}},
    {"t09-buffer-recycling", {"Buffer Requeue Delay Tolerance", v4l2diag::TestLayer::BufferMemory, "buffering", {"stable"}}},
    {"t10-buffer-flags", {"V4L2 Buffer Flag Analysis", v4l2diag::TestLayer::BufferMemory, "metadata", {"stable"}}},
    {"t11-memory-throughput", {"Memory Access Throughput", v4l2diag::TestLayer::BufferMemory, "memory", {"benchmark"}}},
    {"t12-dmabuf-cache-sync", {"DMABUF CPU Read Synchronization", v4l2diag::TestLayer::BufferMemory, "dmabuf", {"device-specific"}}},
    {"t13-poll-timeout-cliff", {"Poll Timeout Reliability Boundary", v4l2diag::TestLayer::PollingTimeout, "polling", {"stable"}}},
    {"t14-trigger-latency", {"Trigger-to-Frame Delivery Latency", v4l2diag::TestLayer::Latency, "latency", {"benchmark"}}},
    {"t15-nonblock-vs-block", {"Non-blocking Spin vs Blocking DQBUF", v4l2diag::TestLayer::Latency, "io-mode", {"device-specific"}}},
    {"t16-gpio-pulse-width", {"Trigger Pulse Width and Edge Detection", v4l2diag::TestLayer::Latency, "trigger", {"device-specific"}}},
    {"t17-format-comparison", {"Pixel Format Performance Comparison", v4l2diag::TestLayer::Latency, "format", {"benchmark"}}},
    {"t18-control-sweep", {"V4L2 Control Value Impact Analysis", v4l2diag::TestLayer::Latency, "controls", {"stress", "benchmark"}}},
    {"t19-resolution-sweep", {"Resolution Capability and Performance", v4l2diag::TestLayer::Latency, "format", {"benchmark"}}},
    {"t20-sequence-continuity", {"Frame Sequence Continuity", v4l2diag::TestLayer::Integrity, "sequence", {"stable"}}},
    {"t21-timestamp-monotonicity", {"Buffer Timestamp Monotonicity", v4l2diag::TestLayer::Integrity, "metadata", {"stable"}}},
    {"t22-stuck-frame", {"Consecutive Frame Content Stability", v4l2diag::TestLayer::Integrity, "quality", {"stable"}}},
    {"t23-sustained-capture", {"Sustained Capture Stability", v4l2diag::TestLayer::Stability, "stability", {"long-running"}}},
    {"t24-latency-under-load", {"CPU Load Impact on Capture Latency", v4l2diag::TestLayer::Stability, "stability", {"benchmark"}}},
    {"t25-multi-camera", {"Multi-camera Capture and Synchronization", v4l2diag::TestLayer::Stability, "stability", {"long-running"}}},
    {"t26-cold-start", {"Post-STREAMON Latency Stabilization", v4l2diag::TestLayer::Stability, "stability", {"stable"}}},
};

}  // namespace

int main() {
  using v4l2diag::TestLayer;
  bool ok = true;
  const auto tests = v4l2diag::built_in_tests();

  ok &= check(tests.size() == kCanonical.size(),
              "the registry holds " + std::to_string(tests.size()) + " tests, expected " +
                  std::to_string(kCanonical.size()));

  // Every registered test matches the canonical table, and nothing is missing.
  std::set<std::string> seen;
  for (const auto &test : tests) {
    const auto it = kCanonical.find(test.id);
    if (!check(it != kCanonical.end(), "unexpected test id in the registry: " + test.id)) {
      ok = false;
      continue;
    }
    seen.insert(test.id);
    ok &= check(test.name == it->second.name,
                test.id + " name is \"" + test.name + "\", expected \"" + it->second.name + "\"");
    ok &= check(test.layer == it->second.layer,
                test.id + " layer is " + std::to_string(v4l2diag::layer_number(test.layer)) + ", expected " +
                    std::to_string(v4l2diag::layer_number(it->second.layer)));
    // Technical identity must survive a rename, exactly. These feed selector
    // resolution and the "stable" default set.
    ok &= check(test.category == it->second.category,
                test.id + " category is \"" + test.category + "\", expected \"" + it->second.category + "\"");
    ok &= check(test.tags == it->second.tags,
                test.id + " tags are {" + join(test.tags) + "}, expected {" + join(it->second.tags) + "}");
    // A display name is not an id.
    ok &= check(test.name != test.id, test.id + " name is still the raw id");
  }
  for (const auto &entry : kCanonical) {
    ok &= check(seen.count(entry.first) == 1, "canonical test missing from the registry: " + entry.first);
  }

  // Display names are unique: two tests sharing a heading would be unreadable.
  {
    std::set<std::string> names;
    for (const auto &test : tests) {
      ok &= check(names.insert(test.name).second, "duplicate display name: " + test.name);
    }
  }

  // Layer metadata is complete and ordered 1-7 with the documented wording.
  {
    const std::vector<std::pair<TestLayer, const char *>> expected = {
        {TestLayer::Discovery, "Discovery"},
        {TestLayer::StateMachine, "State-machine correctness"},
        {TestLayer::BufferMemory, "Buffer & memory"},
        {TestLayer::PollingTimeout, "Polling / timeout"},
        {TestLayer::Latency, "Latency"},
        {TestLayer::Integrity, "Integrity"},
        {TestLayer::Stability, "Stability"},
    };
    int n = 1;
    for (const auto &entry : expected) {
      ok &= check(v4l2diag::layer_number(entry.first) == n,
                  std::string("layer_number is wrong for ") + entry.second);
      ok &= check(std::string(v4l2diag::layer_name(entry.first)) == entry.second,
                  std::string("layer_name is \"") + v4l2diag::layer_name(entry.first) + "\", expected \"" +
                      entry.second + "\"");
      n++;
    }

    // Every one of the seven layers is actually used, so no layer name is dead.
    for (const auto &entry : expected) {
      const bool used = std::any_of(tests.begin(), tests.end(),
                                    [&](const v4l2diag::TestDefinition &t) { return t.layer == entry.first; });
      ok &= check(used, std::string("no test is assigned to layer ") + entry.second);
    }
  }

  // Selectors still resolve by the unchanged technical keys, not by display name.
  {
    const auto by_id = v4l2diag::select_tests({"t16-gpio-pulse-width"});
    ok &= check(by_id.size() == 1 && by_id.front().id == "t16-gpio-pulse-width",
                "a test id stopped resolving after the rename");
    ok &= check(v4l2diag::select_tests({"Trigger Pulse Width and Edge Detection"}).empty(),
                "a display name is being accepted as a selector");

    v4l2diag::TestDefinition found;
    ok &= check(v4l2diag::find_test_definition("t22-stuck-frame", &found), "find_test_definition lost a test id");
    ok &= check(found.name == "Consecutive Frame Content Stability", "find_test_definition returned a stale name");
    ok &= check(found.layer == TestLayer::Integrity, "find_test_definition returned no layer");
  }

  // --- API serialisation (GET /api/tests) ---------------------------------
  // The web UI groups by these fields, so they must be on the wire for every
  // test, with the number and the name agreeing.
  {
    for (const auto &test : tests) {
      const Json::Value json = v4l2diag::test_to_json(test);

      ok &= check(json.isMember("layer"), test.id + " serialises without a \"layer\" field");
      ok &= check(json.isMember("layer_name"), test.id + " serialises without a \"layer_name\" field");
      ok &= check(json["layer"].isInt(), test.id + " serialises \"layer\" as a non-integer");
      ok &= check(json["layer"].asInt() == v4l2diag::layer_number(test.layer),
                  test.id + " serialises the wrong layer number");
      ok &= check(json["layer_name"].asString() == v4l2diag::layer_name(test.layer),
                  test.id + " serialises the wrong layer name");
      ok &= check(json["layer"].asInt() >= 1 && json["layer"].asInt() <= 7,
                  test.id + " serialises a layer outside 1-7");

      // The rename must reach the wire, and the technical keys must not move.
      ok &= check(json["name"].asString() == test.name, test.id + " serialises a stale display name");
      ok &= check(json["id"].asString() == test.id, test.id + " serialises a different id");
      ok &= check(json["category"].asString() == test.category, test.id + " serialises a different category");
      ok &= check(json["tags"].isArray() && json["tags"].size() == test.tags.size(),
                  test.id + " serialises the wrong tag count");
    }

    // Representative field spot-check on one payload, so a field rename in
    // test_to_json cannot pass unnoticed. The per-test loop above already
    // covers every test for the fields the UI depends on.
    v4l2diag::TestDefinition t12;
    ok &= check(v4l2diag::find_test_definition("t12-dmabuf-cache-sync", &t12), "t12 is missing");
    const Json::Value json = v4l2diag::test_to_json(t12);
    ok &= check(json["id"].asString() == "t12-dmabuf-cache-sync", "t12 id changed on the wire");
    ok &= check(json["name"].asString() == "DMABUF CPU Read Synchronization", "t12 name changed on the wire");
    ok &= check(json["layer"].asInt() == 3, "t12 layer number changed on the wire");
    ok &= check(json["layer_name"].asString() == "Buffer & memory", "t12 layer name changed on the wire");
    ok &= check(json["category"].asString() == "dmabuf", "t12 technical category changed on the wire");
  }

  // The exact tags above are what "stable" resolves to, and the exact categories
  // are what a category selector resolves to. Check both through select_tests()
  // so the table cannot drift away from real selector behaviour.
  {
    std::vector<std::string> expected_stable;
    std::map<std::string, std::vector<std::string>> expected_by_category;
    for (const auto &entry : kCanonical) {
      if (std::find(entry.second.tags.begin(), entry.second.tags.end(), "stable") != entry.second.tags.end()) {
        expected_stable.push_back(entry.first);
      }
      expected_by_category[entry.second.category].push_back(entry.first);
    }

    std::vector<std::string> actual_stable;
    for (const auto &test : v4l2diag::select_tests({"stable"})) {
      actual_stable.push_back(test.id);
    }
    std::sort(actual_stable.begin(), actual_stable.end());
    std::sort(expected_stable.begin(), expected_stable.end());
    ok &= check(actual_stable == expected_stable,
                "the \"stable\" tag resolves to " + std::to_string(actual_stable.size()) + " tests, the table says " +
                    std::to_string(expected_stable.size()));

    // An empty selector list means the stable set, so the two must agree.
    std::vector<std::string> defaulted;
    for (const auto &test : v4l2diag::select_tests({})) {
      defaulted.push_back(test.id);
    }
    std::sort(defaulted.begin(), defaulted.end());
    ok &= check(defaulted == expected_stable, "the default selection no longer equals the stable set");

    for (const auto &entry : expected_by_category) {
      std::vector<std::string> actual;
      for (const auto &test : v4l2diag::select_tests({entry.first})) {
        actual.push_back(test.id);
      }
      std::sort(actual.begin(), actual.end());
      std::vector<std::string> want = entry.second;
      std::sort(want.begin(), want.end());
      ok &= check(actual == want, "category \"" + entry.first + "\" resolves to {" + join(actual) +
                                      "}, the table says {" + join(want) + "}");
    }
  }

  return ok ? 0 : 1;
}
