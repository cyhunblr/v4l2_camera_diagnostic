# t04 — POLLERR/POLLHUP Handling

**Layer:** 2 — State-machine correctness  
**Category:** stream-state  
**Trigger required:** yes  

## Purpose

Verifies that after `VIDIOC_STREAMOFF`, the driver correctly rejects DQBUF attempts, and that the stream can be recovered by calling `VIDIOC_STREAMON` again. This tests the driver's error signaling and recovery path, which is critical for applications that need to stop and restart capture.

## How It Works

1. Opens the device, starts streaming, and warms up with a few captures.
2. Captures a baseline set of frames (3) to confirm normal operation.
3. Calls `VIDIOC_STREAMOFF` to stop the stream.
4. Sends a trigger and calls `poll()` to observe the driver's error signaling behavior (POLLERR/POLLHUP).
5. Attempts `VIDIOC_DQBUF` — this should fail since streaming is off.
6. Calls `VIDIOC_STREAMON` again to restart the stream.
7. Captures recovery frames (3) to confirm the pipeline is functional again.

## Implementation

Function: `run_pollerr_handling` in [diagnostic_runner.cpp](../../../source/backend/core/src/diagnostic_runner.cpp)  
Registry: `t04-pollerr-handling` in [test_registry.cpp](../../../source/backend/core/src/test_registry.cpp)

> The source file contains `// Docs: docs/backend/tests/t04-pollerr-handling.md`
> above the function as a back-reference to this document.

## Parameters

| Key | Default | Unit | Description |
| ----- | --------- | ------ | ------------- |
| `baseline_captures` | 3 | count | Number of baseline frames to capture before STREAMOFF |
| `recovery_captures` | 3 | count | Number of recovery frames to capture after re-STREAMON |
| `poll_timeout_ms` | 100 | ms | Timeout for poll() calls |
| `warmup_count` | 3 | count | Warmup frames before baseline and after recovery restart |

## Output Metrics

| Key | Unit | Description |
| ----- | ------ | ------------- |
| `baseline_ok` | count | Number of baseline frames captured successfully |
| `pollerr_raised` | bool | Whether POLLERR was set in revents after STREAMOFF |
| `pollhup_raised` | bool | Whether POLLHUP was set in revents after STREAMOFF |
| `dqbuf_failed` | bool | Whether DQBUF correctly failed after STREAMOFF |
| `restreamon_ok` | bool | Whether re-STREAMON succeeded |
| `recovery_ok` | count | Number of frames captured after recovery |

## Report Details

```text
poll ret=0 POLLERR
DQBUF after STREAMOFF: failed errno=22 (Invalid argument)
```

## Verdict Logic

| Status | Condition |
| -------- | ----------- |
| **Pass** | DQBUF fails after STREAMOFF AND re-STREAMON succeeds AND recovery_ok ≥ 2 (min_recovery_ok threshold) |
| **Warn** | DQBUF fails correctly but recovery is partial (recovery_ok < min_recovery_ok) |
| **Fail** | DQBUF succeeded after STREAMOFF — state machine violation |

## Interpretation Guide

- `pollerr_raised = 1`: Driver signals POLLERR after STREAMOFF — some drivers do this, some don't. Both are valid.
- `pollhup_raised = 1`: Less common; signals the stream endpoint has disconnected.
- `dqbuf_failed = 1`: Correct behavior — no frames should be available after STREAMOFF.
- `restreamon_ok = 0`: The driver cannot restart streaming after STREAMOFF — critical issue for real applications.
- `recovery_ok < 3`: Recovery works but not all frames arrive — may indicate a timing issue in the recovery path.

## Failure Modes

| Symptom | Likely Cause |
| --------- | -------------- |
| DQBUF succeeded after STREAMOFF | Driver bug — buffers not properly invalidated on STREAMOFF |
| `restreamon_ok = 0` | Driver requires full close/reopen cycle to restart streaming |
| `recovery_ok = 0` | Stream restart works at the ioctl level but the pipeline is stalled |
| `baseline_ok = 0` | Trigger not working or camera not producing frames in general |
