#pragma once

#include "v4l2diag/core/types.hpp"

#include <string>
#include <vector>

namespace v4l2diag {

struct TestDefinition {
  std::string id;
  std::string name;
  std::string category;
  std::string description;
  bool uses_trigger = false;
  bool requires_dmabuf = false;
  bool long_running = false;
  bool experimental = false;
  bool risky = false;
  bool implemented_in_core = false;
  unsigned trigger_mode_mask = 0x07;
};

std::vector<TestDefinition> built_in_tests();
std::vector<TestDefinition> select_tests(const std::vector<std::string> &selectors, bool include_long_tests,
                                         bool include_experimental_tests);
bool find_test_definition(const std::string &id, TestDefinition *definition);
bool supports_trigger_mode(const TestDefinition &test, TriggerMode mode);

}  // namespace v4l2diag
