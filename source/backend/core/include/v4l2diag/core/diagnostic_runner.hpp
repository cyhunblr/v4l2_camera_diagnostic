#pragma once

#include "v4l2diag/core/profile_registry.hpp"
#include "v4l2diag/core/test_registry.hpp"
#include "v4l2diag/core/threshold_registry.hpp"
#include "v4l2diag/core/trigger_registry.hpp"
#include "v4l2diag/core/types.hpp"

#include <memory>
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
  // Set before any per-camera work begins and only read thereafter.
  ThresholdConfig active_thresholds_;
  // Shared per-physical-GPIO-line trigger cache: cameras (master + slaves)
  // that resolve to the same chip_id:line_number share one GpioTrigger
  // (and its one background pulse-worker thread) instead of each opening
  // an independent handle to the same pin. Persists for this runner's
  // lifetime.
  TriggerRegistry trigger_registry_;

  CameraRunResult run_camera(const RunConfig::CameraConfig &camera, const RunConfig &config,
                             const DeviceProfile &profile, const std::vector<TestDefinition> &tests);
  TestResult run_test(const std::string &camera_path, MemoryBackend backend, const TestDefinition &definition,
                      const RunConfig &config, const DeviceProfile &profile, TriggerSource *trigger,
                      const std::shared_ptr<TriggerSource> &master_trigger, const std::string &trigger_error);
  // Per-test thresholds from active_thresholds_ (empty map for tests without
  // tunable verdict thresholds).
  const TestThresholds &thresholds_for(const std::string &test_id) const;
  // Per-test run parameters from active_thresholds_.params.
  const TestThresholds &params_for(const std::string &test_id) const;
};

}  // namespace v4l2diag
