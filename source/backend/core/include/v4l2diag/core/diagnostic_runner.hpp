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

  // `role` is this camera's run role and `routing` the run-level resolution; the
  // channel comes from the two together rather than from a per-camera field.
  CameraRunResult run_camera(const RunConfig::CameraConfig &camera, const RunConfig &config,
                             const DeviceProfile &profile, const std::vector<TestDefinition> &tests,
                             const std::string &role, const RoleResolution &routing);
  // `routing` is needed by t25, which opens the slaves itself and looks each one's
  // channel up by its slave-N role.
  TestResult run_test(const std::string &camera_path, MemoryBackend backend, const TestDefinition &definition,
                      const RunConfig &config, const DeviceProfile &profile, TriggerSource *trigger,
                      const std::shared_ptr<TriggerSource> &master_trigger, const std::string &trigger_error,
                      const RoleResolution &routing);
  // Per-test thresholds from active_thresholds_ (empty map for tests without
  // tunable verdict thresholds).
  const TestThresholds &thresholds_for(const std::string &test_id) const;
  // Per-test run parameters from active_thresholds_.params.
  const TestThresholds &params_for(const std::string &test_id) const;
};

// --- Verdict rules extracted for testing ---------------------------------
//
// The test bodies need a real V4L2 device, so the decisions they make are
// declared here and unit-tested in tests/verdict_rules_test.cpp.

// One requested buffer count in the t07 sweep.
struct MultiBufferOutcome {
  int requested = 0;
  // Buffers the driver actually granted. Informational: a clamp is normal
  // driver behaviour and never changes the verdict on its own.
  int allocated = 0;
  // open + REQBUFS succeeded and, when a trigger was available, STREAMON too.
  bool setup_ok = false;
  // 0 when capture was deliberately skipped (no trigger); then there is
  // nothing to miss and the allocation evidence stands on its own.
  int samples_requested = 0;
  int samples_captured = 0;
};

// t07 verdict, per report-ui-review-plan.md §5.7.3:
//   FAIL  no configuration reached a usable session (or the sweep is empty)
//   WARN  some configuration failed, or frames were missed, but at least one
//         configuration is usable
//   PASS  every configuration worked and captured every sample it requested
// Configurations that allocated, started AND delivered at least one frame.
int multi_buffer_usable_count(const std::vector<MultiBufferOutcome> &outcomes);
TestStatus multi_buffer_verdict(const std::vector<MultiBufferOutcome> &outcomes);

// One pulse width in the t16 sweep.
struct PulseWidthOutcome {
  int width_ms = 0;
  int samples_requested = 0;
  int samples_captured = 0;
};

// t16 verdict, per report-ui-review-plan.md §5.16.8:
//   FAIL  no width produced a frame (session failures are handled earlier)
//   WARN  the sweep ran but a width had partial hits, or the edge evidence
//         was inconclusive
//   PASS  the sweep completed and the edge evidence is conclusive
TestStatus pulse_width_verdict(const std::vector<PulseWidthOutcome> &outcomes, bool edge_evidence_conclusive);

}  // namespace v4l2diag
