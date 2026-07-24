# t20 — Timestamp Monotonicity

**Layer:** 6 — Integrity  
**Category:** metadata  
**Trigger required:** yes  

## Purpose

Validates that the kernel-assigned buffer timestamps (`v4l2_buffer.timestamp`) increase monotonically across consecutive frames and characterises the timing relationship between hardware timestamps and wall-clock receive times. Non-monotonic timestamps break any algorithm that relies on inter-frame deltas for motion estimation, exposure bracketing, or synchronisation with external sensors.

## How It Works

1. Opens a V4L2 session with 2 buffers and performs warmup.
2. Captures `sample_count` frames, recording for each:
   - **buf_ts** — the `v4l2_buffer.timestamp` (microseconds since epoch).
   - **wall_ts** — the wall-clock time at the moment DQBUF returns (via `clock_gettime`).
3. Computes inter-frame deltas: `delta[i] = (buf_ts[i] − buf_ts[i−1]) / 1000` (ms).
4. Counts non-monotonic occurrences where `buf_ts[i] ≤ buf_ts[i−1]`.
5. Computes the wall-to-buffer offset for every frame: `offset[i] = (wall_ts[i] − buf_ts[i]) / 1000` (ms).
6. Pushes full statistical summaries (mean, stddev, min, max, p95, jitter) for both delta and offset distributions.
7. Renders verdict based on the non-monotonic count.

## Implementation

Function: `run_timestamp_monotonicity` in [diagnostic_runner.cpp](../../../source/backend/core/src/diagnostic_runner.cpp)  
Registry: `t20-timestamp-monotonicity` in [test_registry.cpp](../../../source/backend/core/src/test_registry.cpp)

> The source file contains `// Docs: docs/backend/tests/t20-timestamp-monotonicity.md`
> above the function as a back-reference to this document.

## Parameters

| Key | Default | Unit | Description |
| ----- | --------- | ------ | ------------- |
| `sample_count` | 100 | count | Number of frames to capture. |
| `capture_timeout_ms` | 100 | ms | Per-frame poll timeout. |
| `sample_interval_ms` | 100 | ms | Delay between consecutive captures. |

## Output Metrics

### Inter-frame delta statistics (`delta_*`)

| Metric Key | Unit | Description |
| ----------- | ------ | ------------- |
| `delta_mean` | ms | Mean time between consecutive buffer timestamps. |
| `delta_stddev` | ms | Standard deviation of inter-frame deltas. |
| `delta_min` | ms | Minimum inter-frame delta observed. |
| `delta_max` | ms | Maximum inter-frame delta observed. |
| `delta_p95` | ms | 95th percentile of inter-frame deltas. |
| `delta_jitter` | ms | Jitter (max − min) of inter-frame deltas. |

### Wall-to-buffer offset statistics (`wall_buf_offset_*`)

| Metric Key | Unit | Description |
| ----------- | ------ | ------------- |
| `wall_buf_offset_mean` | ms | Mean offset between wall clock and buffer timestamp. |
| `wall_buf_offset_stddev` | ms | Standard deviation of the wall-buffer offset. |
| `wall_buf_offset_min` | ms | Minimum wall-buffer offset. |
| `wall_buf_offset_max` | ms | Maximum wall-buffer offset. |
| `wall_buf_offset_p95` | ms | 95th percentile of wall-buffer offset. |
| `wall_buf_offset_jitter` | ms | Jitter of wall-buffer offset. |

### Anomaly count

| Metric Key | Unit | Description |
| ---------- | ---- | ----------- |
| `non_monotonic` | count | Number of frames where timestamp did not increase vs. predecessor. |

## Report Details

No explicit detail lines are pushed. All analysis is captured in the metrics above.

## Verdict Logic

| Status | Condition |
| ------ | --------- |
| **Pass** | `non_monotonic ≤ max_non_monotonic` (default: 0) |
| **Fail** | `non_monotonic > max_non_monotonic` |

Default threshold: `max_non_monotonic = 0` — any single non-monotonic timestamp is a failure.

## Interpretation Guide

- **`delta_mean` ≈ expected frame interval** (e.g. ~33 ms at 30 fps) — sensor and driver are in sync.
- **`delta_stddev` < 2 ms** — very stable timing; low scheduling jitter.
- **`delta_stddev` > 5 ms** — significant jitter; may indicate USB scheduling contention or variable-rate sensor.
- **`wall_buf_offset_mean` large (> 50 ms)** — frames are queued long before the application dequeues them; consider reducing processing time or increasing buffer count.
- **`wall_buf_offset_jitter` high** — inconsistent dequeue timing; the capture thread may be starved by other work.
- **`non_monotonic > 0`** — firmware or driver bug; the timestamp source wrapped, was reset, or uses an unstable clock. Critical for frame-ordered pipelines.

## Failure Modes

| Symptom | Likely Cause |
| --------- | -------------- |
| `non_monotonic > 0` | Driver assigns timestamps from a non-monotonic source (e.g. `CLOCK_REALTIME` with NTP step); or camera firmware resets its internal counter. |
| `delta_min` negative | Same root cause as non-monotonic; timestamps went backwards. |
| `delta_max` >> `delta_mean` | One frame was severely delayed (USB bus contention, thermal throttle, or sensor AE adjustment causing a long exposure). |
| `wall_buf_offset` increases over time | Application falling behind; latency accumulates — the dequeue rate is lower than the production rate. |
| Immediate `Fail` with "Insufficient frames" | Camera not delivering frames; trigger or hardware issue. |
