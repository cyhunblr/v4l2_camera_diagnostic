# t19 — Sequence Continuity

**Layer:** 6 — Integrity  
**Category:** sequence  
**Trigger required:** yes  

## Purpose

Verifies that the V4L2 driver delivers frames with strictly incrementing sequence numbers and no gaps, duplicates, or timestamp reversals. Sequence discontinuities indicate dropped frames at the driver or hardware level that the application cannot recover, making this a critical integrity check for any frame-counting or synchronisation pipeline.

## How It Works

1. Opens a V4L2 session on the target camera with 2 buffers.
2. Performs a warmup capture to stabilise the pipeline.
3. Captures `sample_count` frames, recording the `v4l2_buffer.sequence` number and kernel timestamp of each.
4. Iterates through consecutive frame pairs, tallying:
   - **Gaps** — sequence jumps greater than 1 (each gap contributes `gap − 1` dropped frames).
   - **Duplicates** — sequence difference of 0.
   - **Non-monotonic timestamps** — current timestamp ≤ previous timestamp.
5. Computes the largest single gap encountered.
6. Reports metrics and renders a verdict based on configured thresholds.

## Implementation

Function: `run_sequence_continuity` in [diagnostic_runner.cpp](../../../source/backend/core/src/diagnostic_runner.cpp)  
Registry: `t19-sequence-continuity` in [test_registry.cpp](../../../source/backend/core/src/test_registry.cpp)

> The source file contains `// Docs: docs/backend/tests/t19-sequence-continuity.md`
> above the function as a back-reference to this document.

## Parameters

| Key | Default | Unit | Description |
| ----- | --------- | ------ | ------------- |
| `sample_count` | 100 | count | Number of frames to capture. |
| `capture_timeout_ms` | 100 | ms | Per-frame poll timeout. |
| `sample_interval_ms` | 100 | ms | Delay between consecutive captures. |

## Output Metrics

| Metric Key | Unit | Description |
| ----------- | ------ | ------------- |
| `frames_captured` | count | Total frames successfully received. |
| `dropped_frames` | count | Sum of all sequence gaps (total missing frames). |
| `max_gap` | count | Largest single sequence jump (frames missing in one burst). |
| `duplicates` | count | Number of consecutive frame pairs with the same sequence number. |
| `ts_non_monotonic` | count | Number of consecutive frame pairs where the timestamp did not increase. |

## Report Details

```text
Sequence range: 1042 → 1141
```

Shows the first and last `v4l2_buffer.sequence` values in the capture window.

## Verdict Logic

| Status | Condition |
| -------- | ----------- |
| **Pass** | `dropped_frames == 0` AND `duplicates == 0` AND `ts_non_monotonic == 0` |
| **Warn** | Any anomaly present but `dropped_frames ≤ max_dropped_frames` AND `ts_non_monotonic ≤ max_non_monotonic` |
| **Fail** | `dropped_frames > max_dropped_frames` OR `ts_non_monotonic > max_non_monotonic` |

Default thresholds: `max_dropped_frames = 5`, `max_non_monotonic = 0`.

## Interpretation Guide

- **`dropped_frames = 0`** — the driver delivers every frame produced by the sensor; the pipeline has enough bandwidth and buffers.
- **Small `dropped_frames` (1–5)** — occasional pressure on the buffer queue (typically transient scheduling delays); usually acceptable in non-real-time systems.
- **Large `dropped_frames`** — systematic starvation; the application is not re-queuing buffers fast enough, or USB/PCIe bandwidth is saturated.
- **`duplicates > 0`** — the driver is reporting the same sequence number twice; this can indicate a firmware bug or hardware reset mid-stream.
- **`ts_non_monotonic > 0`** — the kernel timestamp clock wrapped or jumped; may signal clock-domain issues or a driver that does not set `v4l2_buffer.timestamp` correctly.
- **`max_gap` significantly larger than `dropped_frames / number of gaps`** — drops are bursty rather than spread, pointing to a periodic interference source (e.g. garbage collection, thermal throttling).

## Failure Modes

| Symptom | Likely Cause |
| --------- | -------------- |
| High `dropped_frames` with steady latency | Application not re-queuing buffers; increase buffer count or reduce processing time. |
| `max_gap` equals total `dropped_frames` | One large burst of drops — check for a competing process monopolising USB bandwidth at that moment. |
| `duplicates > 0` | Driver/firmware regression; camera reset mid-stream; or a hardware issue where the sensor restarts its counter. |
| `ts_non_monotonic > 0` with zero drops | Clock domain mismatch between SOC and camera; driver using `CLOCK_REALTIME` subject to NTP jumps. |
| Immediate `Fail` with "Insufficient frames" | Camera not producing frames at all — verify trigger is connected and device is streaming. |
