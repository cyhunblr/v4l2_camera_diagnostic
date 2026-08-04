#include "v4l2diag/core/test_registry.hpp"

#include "v4l2diag/core/types.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace v4l2diag {

int layer_number(TestLayer layer) {
  return static_cast<int>(layer);
}

const char *layer_name(TestLayer layer) {
  switch (layer) {
    case TestLayer::Discovery:
      return "Discovery";
    case TestLayer::StateMachine:
      return "State-machine correctness";
    case TestLayer::BufferMemory:
      return "Buffer & memory";
    case TestLayer::PollingTimeout:
      return "Polling / timeout";
    case TestLayer::Latency:
      return "Latency";
    case TestLayer::Integrity:
      return "Integrity";
    case TestLayer::Stability:
      return "Stability";
  }
  return "Unknown";
}

std::vector<TestDefinition> built_in_tests() {
  std::vector<TestDefinition> tests = {
      // id, name, category, description,
      // uses_trigger, requires_dmabuf, tags, trigger_mode_mask

      // --- Layer 1: Discovery ---
      {"t01-device-compliance",
       "V4L2 Device Compliance",
       "discovery",
       "Queries capabilities, memory backend support, formats, and frame sizes.",
       false,
       false,
       {"stable"},
       0x07,
       TestLayer::Discovery},
      {"t02-control-inventory",
       "V4L2 Control Inventory",
       "discovery",
       "Enumerates all V4L2 controls with ranges and current values.",
       false,
       false,
       {"stable"},
       0x07,
       TestLayer::Discovery},

      // --- Layer 2: State-machine correctness ---
      {"t03-pipeline-ready",
       "Pipeline Readiness after STREAMON",
       "stream-state",
       "Times the first frame after STREAMON by spinning on DQBUF instead of polling.",
       true,
       false,
       {"stable"},
       0x07,
       TestLayer::StateMachine},
      {"t04-no-streamon",
       "Frame Capture without STREAMON",
       "stream-state",
       "Validates that no frames are delivered before VIDIOC_STREAMON.",
       false,
       false,
       {"stable"},
       0x07,
       TestLayer::StateMachine},
      {"t05-pollerr-handling",
       "STREAMOFF Error Handling and Recovery",
       "stream-state",
       "Checks DQBUF rejection after STREAMOFF and stream recovery.",
       true,
       false,
       {"stress"},
       0x07,
       TestLayer::StateMachine},
      {"t06-stream-cycles",
       "STREAMON/STREAMOFF Cycle Reliability",
       "stream-state",
       "Exercises full and rapid stream setup/teardown cycles.",
       true,
       false,
       {"stress"},
       0x07,
       TestLayer::StateMachine},

      // --- Layer 3: Buffer & memory ---
      {"t07-multi-buffer",
       "Multi-buffer Configurations",
       "buffering",
       "Compares capture behavior across several requested buffer counts.",
       // Captures frames through the trigger for every buffer count, so a
       // missing trigger must yield SKIP (run_test) rather than an
       // allocation-only sweep that proves nothing. Free-run is unaffected:
       // run_camera always supplies a FreeRunTrigger there.
       true,
       false,
       {"stable"},
       0x07,
       TestLayer::BufferMemory},
      {"t08-buffer-overwrite",
       "Buffer Saturation Behavior",
       "buffering",
       "Sends many triggers without DQBUF to observe queued buffer behavior.",
       true,
       false,
       {"stress"},
       0x07,
       TestLayer::BufferMemory},
      {"t09-buffer-recycling",
       "Buffer Requeue Delay Tolerance",
       "buffering",
       "Measures sensitivity to DQBUF-to-QBUF delay.",
       true,
       false,
       {"stable"},
       0x07,
       TestLayer::BufferMemory},
      {"t10-buffer-flags",
       "V4L2 Buffer Flag Analysis",
       "metadata",
       "Collects V4L2 buffer flags and timestamp source flags.",
       true,
       false,
       {"stable"},
       0x07,
       TestLayer::BufferMemory},
      {"t11-memory-throughput",
       "Memory Access Throughput",
       "memory",
       "Benchmarks device-mapped buffer memcpy throughput without streaming.",
       false,
       false,
       {"benchmark"},
       0x07,
       TestLayer::BufferMemory},
      {"t12-dmabuf-cache-sync",
       "DMABUF CPU Read Synchronization",
       "dmabuf",
       "Compares MMAP and DMABUF reads with cache sync.",
       true,
       true,
       {"device-specific"},
       0x07,
       TestLayer::BufferMemory},

      // --- Layer 4: Polling / timeout ---
      {"t13-poll-timeout-cliff",
       "Poll Timeout Reliability Boundary",
       "polling",
       "Finds the stable poll timeout cliff via adaptive sweep and stability tracking.",
       true,
       false,
       {"stable"},
       0x07,
       TestLayer::PollingTimeout},

      // --- Layer 5: Latency ---
      {"t14-trigger-latency",
       "Trigger-to-Frame Delivery Latency",
       "latency",
       "Measures trigger to received frame latency.",
       true,
       false,
       {"benchmark"},
       0x07,
       TestLayer::Latency},
      {"t15-nonblock-vs-block",
       "Non-blocking Spin vs Blocking DQBUF",
       "io-mode",
       "Compares non-blocking spin behavior with blocking DQBUF behavior.",
       true,
       false,
       {"device-specific"},
       0x07,
       TestLayer::Latency},
      {"t16-gpio-pulse-width",
       "Trigger Pulse Width and Edge Detection",
       "trigger",
       "Sweeps GPIO pulse width to infer trigger edge behavior.",
       true,
       false,
       {"device-specific"},
       0x07,
       TestLayer::Latency},
      {"t17-format-comparison",
       "Pixel Format Performance Comparison",
       "format",
       "Compares supported capture formats and copy throughput.",
       true,
       false,
       {"benchmark"},
       0x07,
       TestLayer::Latency},
      {"t18-control-sweep",
       "V4L2 Control Value Impact Analysis",
       "controls",
       "Sweeps writable V4L2 controls and measures latency effect per combination.",
       true,
       false,
       {"stress", "benchmark"},
       0x07,
       TestLayer::Latency},
      {"t19-resolution-sweep",
       "Resolution Capability and Performance",
       "format",
       "Measures latency and throughput at each supported resolution.",
       true,
       false,
       {"benchmark"},
       0x07,
       TestLayer::Latency},

      // --- Layer 6: Integrity ---
      {"t20-sequence-continuity",
       "Frame Sequence Continuity",
       "sequence",
       "Checks sequence gaps, duplicates, and timestamp monotonicity.",
       true,
       false,
       {"stable"},
       0x07,
       TestLayer::Integrity},
      {"t21-timestamp-monotonicity",
       "Buffer Timestamp Monotonicity",
       "metadata",
       "Checks V4L2 buffer timestamp monotonicity and wall clock offsets.",
       true,
       false,
       {"stable"},
       0x07,
       TestLayer::Integrity},
      {"t22-stuck-frame",
       "Consecutive Frame Content Stability",
       "quality",
       "Compares consecutive frames byte-by-byte to detect a frozen camera output.",
       true,
       false,
       {"stable"},
       0x07,
       TestLayer::Integrity},

      // --- Layer 7: Stability ---
      {"t23-sustained-capture",
       "Sustained Capture Stability",
       "stability",
       "Runs a long capture session and detects drift or sustained misses.",
       true,
       false,
       {"long-running"},
       0x07,
       TestLayer::Stability},
      {"t24-latency-under-load",
       "CPU Load Impact on Capture Latency",
       "stability",
       "Measures trigger-to-DQBUF latency while all CPU cores are saturated.",
       true,
       false,
       {"benchmark"},
       0x07,
       TestLayer::Stability},
      {"t25-multi-camera",
       "Multi-camera Capture and Synchronization",
       "stability",
       "Measures cross-device latency jitter under concurrent capture.",
       true,
       false,
       {"long-running"},
       0x07,
       TestLayer::Stability},
      {"t26-cold-start",
       "Post-STREAMON Latency Stabilization",
       "stability",
       "Measures frames needed to reach steady-state latency after STREAMON.",
       true,
       false,
       {"stable"},
       0x07,
       TestLayer::Stability},
  };
  for (auto &test : tests) {
    if (test.id == "t08-buffer-overwrite" || test.id == "t14-trigger-latency") {
      test.trigger_mode_mask = 0x01 | 0x02;  // Hardware + Software only
    } else if (test.id == "t09-buffer-recycling") {
      test.trigger_mode_mask = 0x04;  // FreeRun only
    } else if (test.id == "t16-gpio-pulse-width") {
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

std::vector<TestDefinition> select_tests(const std::vector<std::string> &selectors,
                                         std::vector<std::string> *unmatched) {
  const auto tests = built_in_tests();
  std::vector<TestDefinition> selected;

  const auto add_unique = [&](const TestDefinition &test) {
    const bool already = std::any_of(selected.begin(), selected.end(),
                                     [&](const TestDefinition &existing) { return existing.id == test.id; });
    if (!already) {
      selected.push_back(test);
    }
  };

  if (selectors.empty()) {
    for (const auto &test : tests) {
      if (std::find(test.tags.begin(), test.tags.end(), "stable") != test.tags.end()) {
        add_unique(test);
      }
    }
    return selected;
  }

  for (const std::string &selector : selectors) {
    if (selector == "all") {
      for (const auto &test : tests) {
        add_unique(test);
      }
      continue;
    }
    const auto by_id =
        std::find_if(tests.begin(), tests.end(), [&](const TestDefinition &test) { return test.id == selector; });
    if (by_id != tests.end()) {
      add_unique(*by_id);
      continue;
    }
    bool matched = false;
    for (const auto &test : tests) {
      if (test.category == selector || std::find(test.tags.begin(), test.tags.end(), selector) != test.tags.end()) {
        add_unique(test);
        matched = true;
      }
    }
    if (!matched && unmatched && std::find(unmatched->begin(), unmatched->end(), selector) == unmatched->end()) {
      // Report a selector once however many times the caller repeated it, so
      // the run log carries one warning per distinct bad selector.
      unmatched->push_back(selector);
    }
  }

  return selected;
}

std::vector<TestDefinition> select_tests(const std::vector<std::string> &selectors) {
  return select_tests(selectors, nullptr);
}

}  // namespace v4l2diag
