#include "v4l2diag/core/test_registry.hpp"

#include "v4l2diag/core/types.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace v4l2diag {

std::vector<TestDefinition> built_in_tests() {
  std::vector<TestDefinition> tests = {
      // id, name, category, description,
      // uses_trigger, requires_dmabuf, long_running, experimental, risky, implemented_in_core

      // --- Layer 1: Discovery ---
      {"t01-device-compliance", "V4L2 device compliance", "discovery",
       "Queries capabilities, memory backend support, formats, and frame sizes.", false, false, false, false, false,
       true},
      {"t02-control-inventory", "V4L2 control inventory", "discovery",
       "Enumerates all V4L2 controls with ranges and current values.", false, false, false, false, false, true},

      // --- Layer 2: State-machine correctness ---
      {"t03-no-streamon", "Frame capture without STREAMON", "stream-state",
       "Validates that no frames are delivered before VIDIOC_STREAMON.", false, false, false, false, false, true},
      {"t04-pollerr-handling", "POLLERR/POLLHUP handling", "stream-state",
       "Checks DQBUF rejection after STREAMOFF and stream recovery.", true, false, false, true, true, true},
      {"t05-stream-cycles", "STREAMON/STREAMOFF cycle reliability", "stream-state",
       "Exercises full and rapid stream setup/teardown cycles.", true, false, false, false, false, true},

      // --- Layer 3: Buffer & memory ---
      {"t06-multi-buffer", "Multi-buffer configurations", "buffering",
       "Compares capture behavior across several requested buffer counts.", false, false, false, false, false, true},
      {"t07-buffer-overwrite", "Buffer overwrite behavior", "buffering",
       "Sends many triggers without DQBUF to observe queued buffer behavior.", true, false, false, false, true, true},
      {"t08-buffer-recycling", "Buffer recycling timing", "buffering", "Measures sensitivity to DQBUF-to-QBUF delay.",
       true, false, false, false, false, true},
      {"t09-buffer-flags", "V4L2 buffer flag analysis", "metadata",
       "Collects V4L2 buffer flags and timestamp source flags.", true, false, false, false, false, true},
      {"t10-memory-throughput", "Memory access throughput", "memory",
       "Benchmarks device-mapped buffer memcpy throughput without streaming.", false, false, false, false, false, true},
      {"t11-dmabuf-cache-sync", "DMA_BUF_IOCTL_SYNC cache coherency", "dmabuf",
       "Compares MMAP and DMABUF reads with cache sync.", true, true, false, false, false, true},

      // --- Layer 4: Polling / timeout ---
      {"t12-poll-timeout-cliff", "Poll timeout cliff finder", "polling",
       "Finds the stable poll timeout cliff via adaptive sweep and stability tracking.", true, false, false, false,
       false, true},

      // --- Layer 5: Latency ---
      {"t13-trigger-latency", "Trigger to DQBUF latency", "latency", "Measures trigger to received frame latency.",
       true, false, false, false, false, true},
      {"t14-nonblock-vs-block", "NON_BLOCK vs BLOCK comparison", "io-mode",
       "Compares non-blocking spin behavior with blocking DQBUF behavior.", true, false, false, false, false, true},
      {"t15-gpio-pulse-width", "GPIO pulse width characterization", "trigger",
       "Sweeps GPIO pulse width to infer trigger edge behavior.", true, false, false, false, false, true},
      {"t16-format-comparison", "Format comparison", "format",
       "Compares supported capture formats and copy throughput.", true, false, false, false, false, true},
      {"t17-control-sweep", "Control parameter sweep", "controls",
       "Sweeps writable V4L2 controls and measures latency effect per combination.", true, false, false, true, true,
       true},
      {"t18-resolution-sweep", "Resolution sweep", "format",
       "Measures latency and throughput at each supported resolution.", true, false, false, false, false, true},

      // --- Layer 6: Integrity ---
      {"t19-sequence-continuity", "Sequence number continuity", "sequence",
       "Checks sequence gaps, duplicates, and timestamp monotonicity.", true, false, false, false, false, true},
      {"t20-timestamp-monotonicity", "Timestamp monotonicity", "metadata",
       "Checks V4L2 buffer timestamp monotonicity and wall clock offsets.", true, false, false, false, false, true},
      {"t21-stuck-frame", "Stuck frame detection", "quality",
       "Compares consecutive frames byte-by-byte to detect a frozen camera output.", true, false, false, false, false,
       true},

      // --- Layer 7: Stability ---
      {"t22-sustained-capture", "Sustained capture stability", "stability",
       "Runs a long capture session and detects drift or sustained misses.", true, false, true, false, false, true},
      {"t23-latency-under-load", "Latency under CPU load", "stability",
       "Measures trigger-to-DQBUF latency while all CPU cores are saturated.", true, false, false, false, false, true},
      {"t24-max-fps", "Maximum frame rate", "stability",
       "Measures maximum sustainable frame rate for the configured trigger mode.", true, false, true, false, false,
       true},
      {"t25-multi-camera", "Multi-camera contention", "stability",
       "Measures cross-device latency jitter under concurrent capture.", true, false, true, false, false, true},
      {"t26-cold-start", "Cold-start warm-up cost", "stability",
       "Measures frames needed to reach steady-state latency after STREAMON.", true, false, false, false, false, true},
  };
  for (auto &test : tests) {
    if (test.id == "t07-buffer-overwrite" || test.id == "t13-trigger-latency") {
      test.trigger_mode_mask = 0x01 | 0x02;  // Hardware + Software only
    } else if (test.id == "t15-gpio-pulse-width") {
      test.trigger_mode_mask = 0x01;  // Hardware only
    }
  }
  return tests;
}

bool supports_trigger_mode(const TestDefinition &test, TriggerMode mode) {
  const unsigned bit = mode == TriggerMode::Hardware ? 0x01 : mode == TriggerMode::Software ? 0x02 : 0x04;
  return (test.trigger_mode_mask & bit) != 0;
}

bool find_test_definition(const std::string &id, TestDefinition *definition) {
  const auto tests = built_in_tests();
  auto it = std::find_if(tests.begin(), tests.end(), [&](const TestDefinition &test) { return test.id == id; });
  if (it == tests.end()) {
    return false;
  }
  *definition = *it;
  return true;
}

std::vector<TestDefinition> select_tests(const std::vector<std::string> &selectors, bool include_long_tests,
                                         bool include_experimental_tests) {
  const auto tests = built_in_tests();
  std::vector<TestDefinition> selected;

  auto eligible = [&](const TestDefinition &test) {
    if (test.long_running && !include_long_tests) {
      return false;
    }
    if (test.experimental && !include_experimental_tests) {
      return false;
    }
    return true;
  };

  const auto append_if = [&](const auto &predicate) {
    for (const auto &test : tests) {
      if (eligible(test) && predicate(test)) {
        const bool already = std::any_of(selected.begin(), selected.end(),
                                         [&](const TestDefinition &existing) { return existing.id == test.id; });
        if (!already) {
          selected.push_back(test);
        }
      }
    }
  };

  if (selectors.empty()) {
    append_if([](const TestDefinition &test) { return test.implemented_in_core; });
    return selected;
  }

  for (const std::string &selector : selectors) {
    if (selector == "all") {
      append_if([](const TestDefinition &) { return true; });
    } else if (selector == "stable") {
      append_if([](const TestDefinition &test) { return !test.experimental && !test.risky; });
    } else if (selector == "implemented") {
      append_if([](const TestDefinition &test) { return test.implemented_in_core; });
    } else {
      append_if([&](const TestDefinition &test) { return test.id == selector || test.category == selector; });
    }
  }

  return selected;
}

}  // namespace v4l2diag
