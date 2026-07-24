# t21 — Stuck Frame

**Layer:** 6 — Integrity  
**Category:** quality  
**Trigger required:** yes  

## Purpose

Detects whether the camera is delivering identical (stuck/frozen) frame content across consecutive captures. A "stuck frame" condition means the sensor or ISP has stopped updating the frame buffer — the application still receives DQBUF completions with new sequence numbers, but the pixel data is unchanged. This is a critical quality defect invisible to sequence-number or timestamp checks alone.

## How It Works

1. Opens a V4L2 session with 2 buffers and warms up the pipeline.
2. Captures up to `sample_count` frames with manual buffer management (deferred requeue).
3. For each captured frame, compares the first `compare_bytes` bytes of the current frame against the previous frame using `memcmp`.
4. Tracks:
   - Total number of identical consecutive pairs.
   - Length of the longest run of identical frames.
5. Requeues each buffer immediately after comparison.
6. Reports metrics and applies a tiered verdict based on the longest identical run.

## Implementation

Function: `run_stuck_frame` in [diagnostic_runner.cpp](../../../source/backend/core/src/diagnostic_runner.cpp)  
Registry: `t21-stuck-frame` in [test_registry.cpp](../../../source/backend/core/src/test_registry.cpp)

> The source file contains `// Docs: docs/backend/tests/t21-stuck-frame.md`
> above the function as a back-reference to this document.

## Parameters

| Key | Default | Unit | Description |
|-----|---------|------|-------------|
| `sample_count` | 50 | count | Maximum frames to capture and compare. |
| `compare_bytes` | 4096 | bytes | Number of leading bytes compared between consecutive frames. |
| `capture_timeout_ms` | 100 | ms | Per-frame poll timeout. |

## Output Metrics

| Metric Key | Unit | Description |
|-----------|------|-------------|
| `frames_tested` | count | Frames successfully captured and compared (may be less than `sample_count` if some captures time out). |
| `identical_pairs` | count | Number of consecutive frame pairs with identical content in the comparison window. |
| `max_identical_run` | count | Longest consecutive streak of identical frames. |

## Report Details

No explicit detail lines are pushed. The summary message describes the outcome.

## Verdict Logic

| Status | Condition |
|--------|-----------|
| **Pass** | `identical_pairs == 0` — every frame is unique. |
| **Warn** | `identical_pairs > 0` but `max_identical_run < max_identical_run` threshold. |
| **Fail** | `max_identical_run ≥ max_identical_run` threshold (default: 5), or fewer than 2 frames captured. |

Default threshold: `max_identical_run = 5`.

## Interpretation Guide

- **`identical_pairs = 0`** — the sensor is producing genuinely new data for every frame; no stuck condition.
- **`identical_pairs` small (1–2) with `max_identical_run = 1`** — occasional duplicate (e.g. exposure time equals frame interval causing identical frames under static scene). Usually benign in static test fixtures.
- **`max_identical_run ≥ 5`** — the camera is likely frozen. The ISP or sensor has stalled; the driver continues to cycle buffers with stale data.
- **Very high `identical_pairs` but low `max_identical_run`** — intermittent stalls; may indicate thermal throttling or a sensor that periodically pauses.
- **Note:** If testing against a perfectly static scene (e.g. lens cap on), even a healthy camera will produce identical frames. Use a scene with noise or a blinking LED for meaningful results.

## Failure Modes

| Symptom | Likely Cause |
|---------|--------------|
| `max_identical_run` ≥ threshold | Sensor/ISP frozen. Possible firmware hang, clock loss to sensor, or I²C communication failure preventing new frame production. |
| `identical_pairs` equals `frames_tested - 1` | Camera is completely stuck for the entire capture window. Power-cycle or driver reload required. |
| `frames_tested` much less than `sample_count` | Many capture timeouts; the camera is intermittently delivering frames. May precede a full stuck condition. |
| False positive with lens cap | Expected behaviour — physical scene produces no photon noise variation. Ensure test fixture has at least some dynamic element. |
| `compare_bytes` too small misses stuck condition | If the first 4 KB is a fixed header/metadata region that never changes, increase `compare_bytes` to include actual pixel data. |
