# t09 — V4L2 Buffer Flag Analysis

**Layer:** 3 — Buffer & memory  
**Category:** metadata  
**Trigger required:** yes  

## Purpose

Collects and analyzes the V4L2 buffer flags reported on each dequeued frame over a series of captures. This reveals the timestamp source type, error flag frequency, and other driver-reported frame metadata that is critical for understanding how the driver timestamps and validates frames.

## How It Works

1. Opens the device, starts streaming with 2 buffers, and warms up.
2. Captures 50 frames, recording the `flags` field from each `v4l2_buffer`.
3. For each frame, checks for the presence of specific flag bits:
   - `V4L2_BUF_FLAG_ERROR` — frame has an error
   - `V4L2_BUF_FLAG_KEYFRAME` — frame is a keyframe
   - `V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC` — monotonic timestamp source
   - `V4L2_BUF_FLAG_TIMESTAMP_COPY` — copied timestamp source
   - `V4L2_BUF_FLAG_TSTAMP_SRC_SOE` — timestamp from start of exposure
   - (else) `TSTAMP_SRC_EOF` — timestamp from end of frame
4. Computes the OR of all flags across all frames and checks timestamp source consistency.
5. Reports individual flag counts and the combined flags bitmask.

## Implementation

Function: `run_buffer_flags` in [diagnostic_runner.cpp](../../../source/backend/core/src/diagnostic_runner.cpp)  
Registry: `t09-buffer-flags` in [test_registry.cpp](../../../source/backend/core/src/test_registry.cpp)

> The source file contains `// Docs: docs/backend/tests/t09-buffer-flags.md`
> above the function as a back-reference to this document.

## Parameters

| Key | Default | Unit | Description |
| ----- | --------- | ------ | ------------- |
| `sample_count` | 50 | count | Number of frames to capture and analyze |
| `capture_timeout_ms` | 100 | ms | Timeout for each capture attempt |
| `sample_interval_ms` | 100 | ms | Delay between captures |

## Output Metrics

| Key | Unit | Description |
| ----- | ------ | ------------- |
| `frames_captured` | count | Total frames successfully captured |
| `flag_error` | count | Frames with V4L2_BUF_FLAG_ERROR set |
| `flag_keyframe` | count | Frames with V4L2_BUF_FLAG_KEYFRAME set |
| `flag_ts_monotonic` | count | Frames with V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC set |
| `flag_ts_copy` | count | Frames with V4L2_BUF_FLAG_TIMESTAMP_COPY set |
| `flag_soe` | count | Frames with V4L2_BUF_FLAG_TSTAMP_SRC_SOE (start-of-exposure timestamp) |
| `flag_eof` | count | Frames with TSTAMP_SRC_EOF (end-of-frame timestamp) |

## Report Details

```text
Combined flags OR: 0x00002002
Timestamp source consistent: yes
```

## Verdict Logic

| Status | Condition |
| ------ | --------- |
| **Pass** | flag_error ≤ 0 (max_error_flags threshold) |
| **Warn** | flag_error > 0 — some frames had V4L2_BUF_FLAG_ERROR |

## Interpretation Guide

- `flag_ts_monotonic = frames_captured`: Driver uses monotonic clock for timestamps — standard and reliable.
- `flag_ts_copy = frames_captured`: Timestamps are user-provided copies — typical for some sensor drivers.
- Both `flag_ts_monotonic = 0` and `flag_ts_copy = 0`: Driver uses the default system clock (acceptable but less precise).
- "Timestamp source consistent: yes": All frames use the same timestamp source — expected behavior.
- "Timestamp source consistent: no": Driver switches between timestamp sources mid-stream — unusual and potentially problematic for timing analysis.
- `flag_soe > 0`: Timestamps represent start-of-exposure — more useful for latency measurement.
- `flag_eof > 0`: Timestamps represent end-of-frame transfer — includes sensor readout time.
- `flag_keyframe` is typically always set for raw video capture (every frame is independent).
- `flag_error > 0`: Some frames arrived with errors — could indicate DMA issues, buffer corruption, or sensor timing problems.

## Failure Modes

| Symptom | Likely Cause |
| --------- | -------------- |
| "Session setup failed" | Cannot open device or start streaming |
| `frames_captured = 0` | Trigger not working or camera not producing frames |
| High `flag_error` count | DMA errors, sensor timing issues, or buffer corruption |
| Inconsistent timestamp source | Driver bug — switching between monotonic and copy mid-stream |
| `flag_ts_monotonic = 0` and `flag_ts_copy = 0` | Older driver that doesn't set explicit timestamp flags |
