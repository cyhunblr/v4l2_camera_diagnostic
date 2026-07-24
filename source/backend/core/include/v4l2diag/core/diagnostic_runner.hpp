#pragma once

#include "v4l2diag/core/profile_registry.hpp"
#include "v4l2diag/core/test_registry.hpp"
#include "v4l2diag/core/threshold_registry.hpp"
#include "v4l2diag/core/types.hpp"

#include <atomic>
#include <string>
#include <vector>

namespace v4l2diag {

class TriggerSource;

class DiagnosticRunner {
 public:
  explicit DiagnosticRunner(ProfileRegistry *profiles);

  RunResult run(const RunConfig &config);

 private:
  ProfileRegistry *profiles_;
  // Verdict thresholds resolved once per run() from config.threshold_config_id.
  // Set before any (possibly parallel) per-camera work begins and only read
  // thereafter, so concurrent reads are safe.
  ThresholdConfig active_thresholds_;
  // t25-multi-camera opens every camera in the run itself, so it must execute
  // at most once per run() regardless of how many cameras/groups dispatch it
  // (including concurrently, under RunMode::Parallel). Reset at the start of
  // each run().
  std::atomic<bool> multi_camera_claimed_{false};

  CameraRunResult run_camera(const RunConfig::CameraConfig &camera, const RunConfig &config,
                             const DeviceProfile &profile, const std::vector<TestDefinition> &tests);
  TestResult run_test(const std::string &camera_path, MemoryBackend backend, const TestDefinition &definition,
                      const RunConfig &config, const DeviceProfile &profile, TriggerSource *trigger,
                      const std::string &trigger_error);
  // Per-test thresholds from active_thresholds_ (empty map for tests without
  // tunable verdict thresholds).
  const TestThresholds &thresholds_for(const std::string &test_id) const;
  // Per-test run parameters from active_thresholds_.params.
  const TestThresholds &params_for(const std::string &test_id) const;
};

}  // namespace v4l2diag
