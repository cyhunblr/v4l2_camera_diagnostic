#pragma once

#include "v4l2diag/core/types.hpp"

#include <string>
#include <vector>

namespace v4l2diag {

// The seven user-facing layers, in report and UI order. This is metadata, not a
// derivation: the frontend used to infer the layer from the test number, so any
// test added past t26 silently fell into an "Other Diagnostics" bucket.
//
// Distinct from `category`, which stays a technical filtering key.
enum class TestLayer {
  Discovery = 1,
  StateMachine = 2,
  BufferMemory = 3,
  PollingTimeout = 4,
  Latency = 5,
  Integrity = 6,
  Stability = 7,
};

// 1-7, the layer's position. Callers order groups by this.
int layer_number(TestLayer layer);
// Short name, e.g. "Buffer & memory". The presentation layer composes the
// "Layer 3 — Buffer & memory" heading from the number and this name.
const char *layer_name(TestLayer layer);

struct TestDefinition {
  std::string id;
  std::string name;
  std::string category;
  std::string description;
  bool uses_trigger = false;
  bool requires_dmabuf = false;
  std::vector<std::string> tags;
  unsigned trigger_mode_mask = 0x07;
  TestLayer layer = TestLayer::Discovery;
};

std::vector<TestDefinition> built_in_tests();
// Resolves selectors (ids, categories, tags, or "all") to test definitions.
// An empty selector list means the "stable" set.
//
// Selectors that match nothing are appended to *unmatched (when non-null) so
// the caller can warn instead of silently running a different set than the
// user asked for. Order is preserved and duplicates collapse -- in the
// result and in *unmatched, so a repeated bad selector warns once.
std::vector<TestDefinition> select_tests(const std::vector<std::string> &selectors,
                                         std::vector<std::string> *unmatched);
std::vector<TestDefinition> select_tests(const std::vector<std::string> &selectors);
bool find_test_definition(const std::string &id, TestDefinition *definition);
bool supports_trigger_mode(const TestDefinition &test, TriggerMode mode);

}  // namespace v4l2diag
