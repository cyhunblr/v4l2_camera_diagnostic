# t07 — Buffer Overwrite Behavior

**Layer:** 3 — Buffer & memory  
**Category:** buffering  
**Trigger required:** yes  

## Purpose

Tests what happens when many triggers are sent without dequeuing buffers — simulating a scenario where the application cannot keep up with the camera's frame rate. This reveals whether the driver overwrites the oldest buffers, drops frames, or sets error flags when the queue is saturated.

## How It Works

1. Two variants are run sequentially, each with a fresh session using 2 buffers:
   - **Variant A:** 100 triggers at 100ms intervals (moderate saturation)
   - **Variant B:** 200 triggers at 50ms intervals (heavy saturation)
2. For each variant:
   a. Opens the device, starts streaming with 2 buffers.
   b. Waits 500ms for settling.
   c. Drains any pre-existing frames.
   d. Sends all triggers without dequeuing.
   e. After all triggers are sent, drains the queue counting available frames and frames with `V4L2_BUF_FLAG_ERROR`.
3. Reports the number of available frames and error-flagged frames per variant.

## Implementation

Function: `run_buffer_overwrite` in [diagnostic_runner.cpp](../../../source/backend/core/src/diagnostic_runner.cpp)  
Registry: `t07-buffer-overwrite` in [test_registry.cpp](../../../source/backend/core/src/test_registry.cpp)

> The source file contains `// Docs: docs/backend/tests/t07-buffer-overwrite.md`
> above the function as a back-reference to this document.

## Parameters

| Key | Default | Unit | Description |
|-----|---------|------|-------------|
| `buffer_count` | 2 | count | Number of buffers to allocate |
| `variant_a_triggers` | 100 | count | Triggers to send in Variant A |
| `variant_a_interval_ms` | 100 | ms | Interval between triggers in Variant A |
| `variant_b_triggers` | 200 | count | Triggers to send in Variant B |
| `variant_b_interval_ms` | 50 | ms | Interval between triggers in Variant B |
| `settle_ms` | 500 | ms | Settling time after stream start before draining |

## Output Metrics

| Key | Unit | Description |
|-----|------|-------------|
| `triggers_A` | count | Number of triggers sent in Variant A |
| `frames_available_A` | count | Frames available after all triggers in Variant A |
| `triggers_B` | count | Number of triggers sent in Variant B |
| `frames_available_B` | count | Frames available after all triggers in Variant B |
| `error_flag_total` | count | Total frames with V4L2_BUF_FLAG_ERROR across both variants |

## Report Details

```
Variant A: buffers=2 triggers=100 available=2
Variant B: buffers=2 triggers=200 available=2 errors=1
```

## Verdict Logic

| Status | Condition |
|--------|-----------|
| **Pass** | error_flag_total ≤ 0 (max_error_flags threshold) |
| **Warn** | error_flag_total > 0 — some frames had V4L2_BUF_FLAG_ERROR |

## Interpretation Guide

- `frames_available_A = 2` (equal to buffer count): Expected behavior — only the most recently written buffers are available for dequeue when the queue is saturated.
- `frames_available > buffer_count`: Unusual — driver may have internally expanded the queue.
- `frames_available = 0`: Possible issue — the driver may have invalidated all buffers on saturation.
- `error_flag_total > 0`: The driver marks overwritten or corrupted frames with the error flag — this is informational, not necessarily a failure.
- With 2 buffers and 100+ triggers, only the last ~2 frames should be dequeue-able — older frames are overwritten in place.

## Failure Modes

| Symptom | Likely Cause |
|---------|--------------|
| "Session setup failed" | Cannot open device or allocate buffers |
| `frames_available = 0` for both variants | Driver discards all buffers on queue overflow |
| Very high `error_flag_total` | Driver detects all overwritten frames as errors |
| `frames_available ≫ buffer_count` | Driver has an internal queue larger than requested (unexpected but not harmful) |
