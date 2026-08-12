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

// t07 verdict, per report-ui-design-spec.md §5.7.3:
//   FAIL  no configuration reached a usable session (or the sweep is empty)
//   WARN  some configuration failed, or frames were missed, but at least one
//         configuration is usable
//   PASS  every configuration worked and captured every sample it requested
// Configurations that allocated, started AND delivered at least one frame.
int multi_buffer_usable_count(const std::vector<MultiBufferOutcome> &outcomes);
TestStatus multi_buffer_verdict(const std::vector<MultiBufferOutcome> &outcomes);

// t05 verdict. The approved t05 preview shows FAIL in all three trigger scenarios, with two
// distinct failure sentences: "The driver returned a frame after STREAMOFF" (the state-machine
// violation) and "DQBUF was correctly rejected after STREAMOFF, but the restarted stream
// delivered no recovery frames (0/3)". The second was reaching WARN, because the code treated
// every non-PASS case with a correctly-failing DQBUF as "recovery partial".
//
// Zero recovery frames is not partial recovery -- it is no recovery. The test's own
// documentation separates these in its Failure Modes table ("recovery_ok = 0: the pipeline is
// stalled"), and t05 carries a single `min_recovery_ok` threshold with no WARN counterpart,
// unlike the tests that genuinely have a warning band (t03, t24 carry pass_*/warn_* pairs).
//
//   FAIL  DQBUF succeeded after STREAMOFF (state-machine violation), or re-STREAMON failed,
//         or the restarted stream delivered NO frames at all
//   WARN  recovery is genuinely partial: at least one frame arrived, but fewer than required
//   PASS  DQBUF failed as it must, re-STREAMON worked, and recovery met the threshold
//
// User decision 2026-08-10 (option A), resolving preview-vs-doc: the preview's FAIL is
// honoured for the zero case while the doc's WARN band is kept for the partial case it
// actually describes.
TestStatus pollerr_recovery_verdict(bool dqbuf_failed, bool restreamon_ok, int recovery_ok, int min_recovery_ok);

// Records the settings a test runs with, as the "key: value" detail lines the report's Test
// Configuration section reads. Keyed on `test->id`, using the same default parameter and threshold
// tables the test bodies resolve through -- so the report states the values the run actually used.
//
// Separate from the test bodies because those need a real V4L2 device: this is the part that can be
// exercised without one, and tests/test_configuration_contract_test.cpp checks its output against
// the rows the approved previews specify. Runners call it once at entry; calling it again is
// harmless but appends duplicates, so it is called from one place per test.
//
// `configured` overrides the built-in defaults where the run was given explicit values (the
// threshold config file, the trigger profile); pass empty maps to record the defaults.
void record_run_parameters(TestResult *test, const TestThresholds &configured_thresholds = {},
                           const TestThresholds &configured_params = {});

// Records a Test Configuration row whose value the run COMPUTED rather than was given -- the
// `derived` source kind the approved previews use for t17's "Sizeimage", t18's "Controls
// discovered", t19's "Pixel format" and the rest.
//
// Separate from record_run_parameters() because the value is only known once the test body has
// run: it cannot come from the parameter tables. Called from the body, after the measurement it
// reports. `unit` is the display unit ("mebibytes", "milliseconds", "" for a bare count).
void record_derived_config(TestResult *test, const std::string &label, const std::string &value,
                           const std::string &unit = "");

// The Test Configuration row labels a test's approved card carries, in order. Empty for a test whose
// approved card has no such section (t01, t02).
//
// The renderer reads this as an ALLOW-LIST: a card shows exactly the rows the design names. It used
// to exclude measurement lines by shape instead, which meant every new runner line shape -- per
// width ("1ms:"), per camera ("/dev/video4:"), per copy ("mmap_full:") -- leaked into the table
// until someone noticed and added another pattern. Three device runs each surfaced a fresh batch.
// Naming what belongs is complete by construction; naming what does not never was.
const std::vector<std::string> &configuration_labels_for(const std::string &test_id);

// One pulse width in the t16 sweep.
struct PulseWidthOutcome {
  int width_ms = 0;
  int samples_requested = 0;
  int samples_captured = 0;
};

// t16 verdict, per report-ui-design-spec.md §5.16.8:
//   FAIL  no width produced a frame (session failures are handled earlier)
//   WARN  the sweep ran but a width had partial hits, or the edge evidence
//         was inconclusive
//   PASS  the sweep completed and the edge evidence is conclusive
TestStatus pulse_width_verdict(const std::vector<PulseWidthOutcome> &outcomes, bool edge_evidence_conclusive);

}  // namespace v4l2diag
