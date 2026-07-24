# t25 — Multi-Camera

**Layer:** 7 — Stability  
**Category:** stability  
**Trigger required:** yes  

## Purpose

Evaluates the system's ability to capture frames from multiple cameras simultaneously with a single shared trigger, measuring cross-device timing jitter. In multi-camera setups (stereo vision, surround monitoring), all cameras must respond to the same trigger within a tight time window — this test quantifies how well the hardware and driver stack maintains synchronisation across devices.

## How It Works

1. **Precondition check:** requires `config.cameras.size() > 1`. If only one camera is configured, the test is skipped.
2. **Run-once guard:** the test opens every configured camera itself, so only the first camera/group to reach `t25-multi-camera` in a given run executes it; any other dispatch (including concurrent ones under `RunMode::Parallel`) is skipped with a note that it already ran.
3. Opens V4L2 sessions on **all** configured camera paths (2 buffers each).
4. Warms up all sessions.
5. For each of `sample_count` rounds:
   - Drains all cameras' buffer queues, then sends a **single** trigger shared across all cameras.
   - Polls all cameras concurrently and measures each camera's latency against that one shared trigger timestamp, so latency reflects true cross-device response rather than a per-camera re-trigger.
   - If all cameras succeed in that round, computes cross-camera jitter: `max_latency − min_latency`.
6. After all rounds, computes per-camera latency statistics and cross-device jitter statistics.
7. Applies verdict based on the p95 of cross-camera jitter.

## Implementation

Function: `run_multi_camera` in [diagnostic_runner.cpp](../../../source/backend/core/src/diagnostic_runner.cpp)  
Registry: `t25-multi-camera` in [test_registry.cpp](../../../source/backend/core/src/test_registry.cpp)

> The source file contains `// Docs: docs/backend/tests/t25-multi-camera.md`
> above the function as a back-reference to this document.

## Parameters

| Key | Default | Unit | Description |
| --- | ------- | ---- | ----------- |
| `sample_count` | 50 | count | Number of trigger-and-capture rounds. |
| `poll_timeout_ms` | 200 | ms | Per-camera poll timeout within each round. |

## Output Metrics

### Per-camera latency statistics (`camN_latency_*`)

For each camera index `N` (0-based), a full stats block is reported:

| Metric Key Pattern | Unit | Description |
| ------------------- | ------ | ------------- |
| `cam0_latency_mean` | ms | Mean capture latency for camera 0. |
| `cam0_latency_stddev` | ms | Standard deviation for camera 0. |
| `cam0_latency_min` | ms | Minimum latency for camera 0. |
| `cam0_latency_max` | ms | Maximum latency for camera 0. |
| `cam0_latency_p95` | ms | 95th percentile for camera 0. |
| `cam0_latency_jitter` | ms | Jitter for camera 0. |

(Repeated for `cam1_latency_*`, `cam2_latency_*`, etc.)

### Cross-camera jitter statistics (`cross_jitter_*`)

| Metric Key | Unit | Description |
| ----------- | ------ | ------------- |
| `cross_jitter_mean` | ms | Mean jitter (max − min latency) across cameras per round. |
| `cross_jitter_stddev` | ms | Standard deviation of cross-camera jitter. |
| `cross_jitter_min` | ms | Minimum cross-camera jitter observed. |
| `cross_jitter_max` | ms | Maximum cross-camera jitter observed. |
| `cross_jitter_p95` | ms | 95th percentile of cross-camera jitter. The primary verdict metric. |
| `cross_jitter_jitter` | ms | Jitter of the cross-camera jitter distribution. |
| `successful_rounds` | count | Rounds where all cameras captured successfully. |

## Report Details

Per-camera capture count lines:

```text
/dev/video0: 50 captures
/dev/video2: 50 captures
Cross-camera jitter mean: 2.340000 ms
```

## Verdict Logic

| Status | Condition |
| -------- | ----------- |
| **Pass** | `cross_jitter_p95 < 5.0` ms |
| **Warn** | `cross_jitter_p95 < 20.0` ms |
| **Fail** | `cross_jitter_p95 ≥ 20.0` ms, or no successful rounds (all cameras never captured in the same round). |

## Interpretation Guide

- **`cross_jitter_p95` < 1 ms** — excellent synchronisation; cameras respond nearly simultaneously to the trigger. Suitable for stereo depth estimation.
- **`cross_jitter_p95` 1–5 ms** — good synchronisation; acceptable for most multi-camera applications.
- **`cross_jitter_p95` 5–20 ms** — moderate desynchronisation; may cause visible artefacts in stitching or stereo matching at high speeds.
- **`cross_jitter_p95` > 20 ms** — cameras are significantly out of sync; the trigger path has different latencies per camera (different USB hubs, interrupt routing, or driver versions).
- **`successful_rounds` much less than `sample_count`** — at least one camera frequently fails; check individual camera health before interpreting jitter.
- **One camera's `camN_latency_mean` significantly higher** — that camera has a longer trigger-to-DQBUF path (e.g. behind a USB hub, or a different sensor model with longer readout time).

## Failure Modes

| Symptom | Likely Cause |
| --------- | -------------- |
| `cross_jitter_p95` > 20 ms | Cameras on different USB controllers with different interrupt latencies; or one camera is on USB 2.0 while another is on USB 3.0. |
| `successful_rounds = 0` | One camera never captures within `poll_timeout_ms`. Likely a camera that doesn't respond to the shared trigger (wrong trigger mode or wiring). |
| Inconsistent jitter (high `cross_jitter_stddev`) | USB bus arbitration or shared interrupt line causing variable priority. Dedicate a USB controller per camera. |
| One `camN_latency_mean` is an outlier | That camera has different firmware, resolution setting, or physical connection quality. Normalise settings across all cameras. |
| Test skipped with "requires multiple cameras" | Only one camera path configured. Add additional camera paths to the run configuration. |
