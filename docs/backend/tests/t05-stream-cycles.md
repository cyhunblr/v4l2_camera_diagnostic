# t05 — STREAMON/STREAMOFF Cycle Reliability

**Layer:** 2 — State-machine correctness  
**Category:** stream-state  
**Trigger modes:** Hardware | Software | FreeRun  

## Purpose

Exercises repeated STREAMON/STREAMOFF cycles to detect resource leaks, race conditions, or state corruption in the kernel driver. Two modes are tested: full cycles (open → start → warmup → capture → close) and rapid cycles (open → start → single capture → close with minimal delay).

## How It Works

1. **Full cycles (20 iterations):** Each cycle opens the device, starts streaming with 2 buffers, warms up (3 frames), captures 5 frames, and closes. A cycle is counted as a failure if any step fails or fewer than 5 frames are captured. The first-frame latency from each cycle is recorded.
2. **Rapid cycles (50 iterations):** Each cycle opens the device, starts streaming, warms up (1 frame, covering sensors that discard their first frame after STREAMON — see [t25](t25-cold-start.md)), captures a single frame, and closes with a settle delay between cycles. This stresses the open/close path.
3. Results are compared against thresholds for full failure count and rapid success percentage.

> **Hardware-protection guard.** Both loops stop early once `max_consecutive_start_failures` open/STREAMON attempts fail back-to-back, and the test reports `Fail`.
>
> This is not a cosmetic limit. Where several sensors sit behind a shared deserializer and fsync source, a STREAMON re-initialises the *whole camera group* over I2C — a single `/dev/videoN` is not independent. Once that re-init starts failing, each further cycle drives the capture channel into another `request timed out after 2500 ms` / `err_rec: attempting to reset the capture channel` round. Left unchecked this degrades into I2C read/write errors, `Power on Camera Sensor failed`, a `vb2_start_streaming` kernel warning, and ultimately a board reset — the diagnostic taking down the machine it is measuring.
>
> A failed STREAMON is therefore recorded (`rapid_start_failures`) rather than silently skipped. Silently skipping it also hides the real fault: a run can report `rapid_cycles_ok = 0` while the true cause is that streaming never started at all, not that frames were missed.

## Implementation

Function: `run_stream_cycles` in [diagnostic_runner.cpp](../../../source/backend/core/src/diagnostic_runner.cpp)  
Registry: `t05-stream-cycles` in [test_registry.cpp](../../../source/backend/core/src/test_registry.cpp)

> The source file contains `// Docs: docs/backend/tests/t05-stream-cycles.md`
> above the function as a back-reference to this document.

## Parameters

| Key | Default | Unit | Description |
| ----- | --------- | ------ | ------------- |
| `full_cycles` | 20 | count | Number of full open/start/capture/close cycles |
| `rapid_cycles` | 50 | count | Number of rapid start/capture/stop cycles |
| `full_warmup` | 3 | count | Warmup frames per full cycle |
| `full_captures` | 5 | count | Frames to capture per full cycle |
| `full_timeout_ms` | 150 | ms | Capture timeout per full-cycle frame |
| `rapid_warmup` | 1 | count | Warmup frames per rapid cycle (absorbs the cold-start frame) |
| `rapid_timeout_ms` | 200 | ms | Capture timeout for the measured rapid-cycle frame |
| `rapid_pacing_ms` | 250 | ms | Settle delay between rapid cycles. Sized for re-initialising a shared camera group over I2C, not for the frame interval |
| `max_consecutive_start_failures` | 3 | count | Back-to-back open/STREAMON failures after which a loop stops early (applies to both loops) |
| `recovery_ms` | 1000 | ms | Pause after a failed open/STREAMON to let the hardware recover before retrying |

## Output Metrics

| Key | Unit | Description |
| ----- | ------ | ------------- |
| `full_cycles_success` | count | Full cycles that completed without error |
| `full_cycle_failures` | count | Full cycles that failed |
| `full_cycles_attempted` | count | Full cycles actually run (below `full_cycles` if the loop stopped early) |
| `full_aborted` | bool | Full loop stopped early by the hardware-protection guard |
| `rapid_cycles_ok` | count | Rapid cycles where a frame was captured |
| `rapid_cycles_total` | count | Configured rapid cycle count |
| `rapid_cycles_attempted` | count | Rapid cycles actually run (below `rapid_cycles` if the loop stopped early) |
| `rapid_start_failures` | count | Rapid cycles where open/STREAMON failed outright |
| `rapid_aborted` | bool | Rapid loop stopped early by the hardware-protection guard |
| `first_frame_latency_mean` | ms | Mean first-frame latency across full cycles |
| `first_frame_latency_max` | ms | Maximum first-frame latency |

## Report Details

Summary line format:

```text
Full: 20/20 OK. Rapid: 48/50 captured.
```

## Verdict Logic

| Status | Condition |
| -------- | ----------- |
| **Fail** | Either loop stopped early on repeated STREAMON failures (checked first) |
| **Pass** | full_failures ≤ 0 AND rapid success % ≥ 90% |
| **Warn** | full_failures ≤ 2 AND rapid success % ≥ 70% |
| **Fail** | full_failures > 2 OR rapid success % < 70% |

Rapid success % is measured against the configured `rapid_cycles`, not against the
number actually attempted — an early stop must not flatter the score.

## Interpretation Guide

- `full_cycle_failures = 0`: Driver handles repeated open/close cleanly with no leaks or state issues.
- `full_cycle_failures > 0`: Possible resource exhaustion or driver state corruption after repeated cycling.
- Low `rapid_cycles_ok` **with `rapid_start_failures = 0`**: frames were missed, but streaming did start — the driver needs more settling time after STREAMOFF.
- Low `rapid_cycles_ok` **with `rapid_start_failures > 0`**: a different fault entirely. Streaming never started on those cycles, so nothing could have been captured. Check the kernel log for capture-channel timeouts, I2C errors, or sensor power-on failures before reading anything into the capture count.
- High `first_frame_latency_max` vs mean: The first frame after STREAMON is occasionally slow — pipeline initialization cost varies.
- Increasing first-frame latency over cycles: Possible memory leak or resource exhaustion in the driver.

## Failure Modes

| Symptom | Likely Cause |
| --------- | -------------- |
| Increasing full_cycle_failures | Kernel resource leak — buffers or file descriptors not released properly |
| rapid_cycles_ok ≪ rapid_cycles_total, rapid_start_failures = 0 | Driver needs more than `rapid_pacing_ms` between STREAMOFF and the next open/start |
| rapid_aborted / full_aborted = 1 | The camera group stopped coming back after STREAMON. On shared-deserializer hardware this is where a runaway loop can reset the board — treat it as a hardware/driver fault, not a tuning problem, and read the kernel log |
| first_frame_latency degrades over time | Memory fragmentation or DMA channel exhaustion |
| All rapid cycles fail | Driver cannot start streaming without a longer settle period, or the sensor discards more than `rapid_warmup` frames after STREAMON — cross-check [t25](t25-cold-start.md) and raise `rapid_warmup` to match |
