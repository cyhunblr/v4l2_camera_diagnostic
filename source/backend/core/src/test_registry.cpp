#include "v4l2diag/core/test_registry.hpp"

#include "v4l2diag/core/types.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace v4l2diag {

std::vector<TestDefinition> built_in_tests() {
  std::vector<TestDefinition> tests = {
      // id, name, category, description,
      // uses_trigger, requires_dmabuf, tags, trigger_mode_mask

      // --- Layer 1: Discovery ---
      {"t01-device-compliance",
       "V4L2 device compliance",
       "discovery",
       "Queries capabilities, memory backend support, formats, and frame sizes.",
       false,
       false,
       {"stable"},
       0x07},
      {"t02-control-inventory",
       "V4L2 control inventory",
       "discovery",
       "Enumerates all V4L2 controls with ranges and current values.",
       false,
       false,
       {"stable"},
       0x07},

      // --- Layer 2: State-machine correctness ---
      {"t03-pipeline-ready",
       "Pipeline readiness after STREAMON",
       "stream-state",
       "Times the first frame after STREAMON by spinning on DQBUF instead of polling.",
       true,
       false,
       {"stable"},
       0x07},
      {"t04-no-streamon",
       "Frame capture without STREAMON",
       "stream-state",
       "Validates that no frames are delivered before VIDIOC_STREAMON.",
       false,
       false,
       {"stable"},
       0x07},
      {"t05-pollerr-handling",
       "POLLERR/POLLHUP handling",
       "stream-state",
       "Checks DQBUF rejection after STREAMOFF and stream recovery.",
       true,
       false,
       {"stress"},
       0x07},
      {"t06-stream-cycles",
       "STREAMON/STREAMOFF cycle reliability",
       "stream-state",
       "Exercises full and rapid stream setup/teardown cycles.",
       true,
       false,
       {"stress"},
       0x07},

      // --- Layer 3: Buffer & memory ---
      {"t07-multi-buffer",
       "Multi-buffer configurations",
       "buffering",
       "Compares capture behavior across several requested buffer counts.",
       false,
       false,
       {"stable"},
       0x07},
      {"t08-buffer-overwrite",
       "Buffer overwrite behavior",
       "buffering",
       "Sends many triggers without DQBUF to observe queued buffer behavior.",
       true,
       false,
       {"stress"},
       0x07},
      {"t09-buffer-recycling",
       "Buffer recycling timing",
       "buffering",
       "Measures sensitivity to DQBUF-to-QBUF delay.",
       true,
       false,
       {"stable"},
       0x07},
      {"t10-buffer-flags",
       "V4L2 buffer flag analysis",
       "metadata",
       "Collects V4L2 buffer flags and timestamp source flags.",
       true,
       false,
       {"stable"},
       0x07},
      {"t11-memory-throughput",
       "Memory access throughput",
       "memory",
       "Benchmarks device-mapped buffer memcpy throughput without streaming.",
       false,
       false,
       {"benchmark"},
       0x07},
      {"t12-dmabuf-cache-sync",
       "DMA_BUF_IOCTL_SYNC cache coherency",
       "dmabuf",
       "Compares MMAP and DMABUF reads with cache sync.",
       true,
       true,
       {"device-specific"},
       0x07},

      // --- Layer 4: Polling / timeout ---
      {"t13-poll-timeout-cliff",
       "Poll timeout cliff finder",
       "polling",
       "Finds the stable poll timeout cliff via adaptive sweep and stability tracking.",
       true,
       false,
       {"stable"},
       0x07},

      // --- Layer 5: Latency ---
      {"t14-trigger-latency",
       "Trigger to DQBUF latency",
       "latency",
       "Measures trigger to received frame latency.",
       true,
       false,
       {"benchmark"},
       0x07},
      {"t15-nonblock-vs-block",
       "NON_BLOCK vs BLOCK comparison",
       "io-mode",
       "Compares non-blocking spin behavior with blocking DQBUF behavior.",
       true,
       false,
       {"device-specific"},
       0x07},
      {"t16-gpio-pulse-width",
       "GPIO pulse width characterization",
       "trigger",
       "Sweeps GPIO pulse width to infer trigger edge behavior.",
       true,
       false,
       {"device-specific"},
       0x07},
      {"t17-format-comparison",
       "Format comparison",
       "format",
       "Compares supported capture formats and copy throughput.",
       true,
       false,
       {"benchmark"},
       0x07},
      {"t18-control-sweep",
       "Control parameter sweep",
       "controls",
       "Sweeps writable V4L2 controls and measures latency effect per combination.",
       true,
       false,
       {"stress", "benchmark"},
       0x07},
      {"t19-resolution-sweep",
       "Resolution sweep",
       "format",
       "Measures latency and throughput at each supported resolution.",
       true,
       false,
       {"benchmark"},
       0x07},

      // --- Layer 6: Integrity ---
      {"t20-sequence-continuity",
       "Sequence number continuity",
       "sequence",
       "Checks sequence gaps, duplicates, and timestamp monotonicity.",
       true,
       false,
       {"stable"},
       0x07},
      {"t21-timestamp-monotonicity",
       "Timestamp monotonicity",
       "metadata",
       "Checks V4L2 buffer timestamp monotonicity and wall clock offsets.",
       true,
       false,
       {"stable"},
       0x07},
      {"t22-stuck-frame",
       "Stuck frame detection",
       "quality",
       "Compares consecutive frames byte-by-byte to detect a frozen camera output.",
       true,
       false,
       {"stable"},
       0x07},

      // --- Layer 7: Stability ---
      {"t23-sustained-capture",
       "Sustained capture stability",
       "stability",
       "Runs a long capture session and detects drift or sustained misses.",
       true,
       false,
       {"long-running"},
       0x07},
      {"t24-latency-under-load",
       "Latency under CPU load",
       "stability",
       "Measures trigger-to-DQBUF latency while all CPU cores are saturated.",
       true,
       false,
       {"benchmark"},
       0x07},
      {"t25-multi-camera",
       "Multi-camera contention",
       "stability",
       "Measures cross-device latency jitter under concurrent capture.",
       true,
       false,
       {"long-running"},
       0x07},
      {"t26-cold-start",
       "Cold-start warm-up cost",
       "stability",
       "Measures frames needed to reach steady-state latency after STREAMON.",
       true,
       false,
       {"stable"},
       0x07},
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

std::vector<TestDefinition> select_tests(const std::vector<std::string> &selectors) {
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
    } else {
      const auto by_id =
          std::find_if(tests.begin(), tests.end(), [&](const TestDefinition &test) { return test.id == selector; });
      if (by_id != tests.end()) {
        add_unique(*by_id);
      } else {
        for (const auto &test : tests) {
          if (test.category == selector || std::find(test.tags.begin(), test.tags.end(), selector) != test.tags.end()) {
            add_unique(test);
          }
        }
      }
    }
  }

  return selected;
}

}  // namespace v4l2diag
