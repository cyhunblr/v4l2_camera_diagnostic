// Validates TriggerRegistry's get_or_create() caching/thread-safety without
// requiring real GPIO hardware. Since no physical gpiochip is guaranteed to
// exist in a host build/CI environment, every open() call below is expected
// to fail — the test instead validates the registry's *behavior around*
// open failures (not caching failed opens, allowing retries) and its
// thread-safety when many threads race on the same/different mappings.

#include "v4l2diag/core/trigger_registry.hpp"

#include <iostream>
#include <string>
#include <thread>
#include <vector>

int main() {
  v4l2diag::TriggerRegistry registry;

  // A mapping pointing at a gpiochip that (almost certainly) doesn't exist
  // on the test host — open() is expected to fail.
  v4l2diag::GpioMapping mapping;
  mapping.chip_id = 987654;
  mapping.line_number = 3;

  std::string error;
  auto first = registry.get_or_create(mapping, &error);
  if (first != nullptr) {
    std::cerr << "expected get_or_create to fail for a nonexistent gpiochip\n";
    return 1;
  }
  if (error.empty()) {
    std::cerr << "expected an error message on failed open\n";
    return 1;
  }

  // A failed open must not be cached — a second attempt should also try
  // (and fail) independently rather than returning a cached null silently.
  std::string second_error;
  auto second = registry.get_or_create(mapping, &second_error);
  if (second != nullptr || second_error.empty()) {
    std::cerr << "expected a second independent failed attempt, not a cached result\n";
    return 1;
  }

  // Thread-safety: many threads racing get_or_create() on the same and on
  // different mappings must not crash, deadlock, or corrupt the registry
  // (every call here fails to open, but the internal map access itself must
  // remain safe under concurrent access).
  {
    std::vector<std::thread> threads;
    for (int i = 0; i < 8; i++) {
      threads.emplace_back([&registry, i] {
        v4l2diag::GpioMapping m;
        m.chip_id = 987654 + (i % 3);  // some threads share a mapping, some don't
        m.line_number = i % 2;
        std::string err;
        registry.get_or_create(m, &err);
      });
    }
    for (auto &t : threads) {
      t.join();
    }
  }

  return 0;
}
