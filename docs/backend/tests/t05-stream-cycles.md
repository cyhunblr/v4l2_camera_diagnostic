# t05 — STREAMON/STREAMOFF Cycle Reliability

**Layer:** 2 — State-machine correctness  
**Category:** stream-state  
**Trigger required:** yes  

## Purpose

Exercises repeated STREAMON/STREAMOFF cycles to detect resource leaks, race conditions, or state corruption in the kernel driver. Two modes are tested: full cycles (open → start → warmup → capture → close) and rapid cycles (open → start → single capture → close with minimal delay).

## How It Works

1. **Full cycles (20 iterations):** Each cycle opens the device, starts streaming with 2 buffers, warms up (3 frames), captures 5 frames, and closes. A cycle is counted as a failure if any step fails or fewer than 5 frames are captured. The first-frame latency from each cycle is recorded.
2. **Rapid cycles (50 iterations):** Each cycle opens the device, starts streaming, captures a single frame (200ms timeout), and closes with only 10ms delay between cycles. This stresses the open/close path.
3. Results are compared against thresholds for full failure count and rapid success percentage.

## Implementation

Function: `run_stream_cycles` in [diagnostic_runner.cpp](../../../source/backend/core/src/diagnostic_runner.cpp)  
Registry: `t05-stream-cycles` in [test_registry.cpp](../../../source/backend/core/src/test_registry.cpp)

> The source file contains `// Docs: docs/backend/tests/t05-stream-cycles.md`
> above the function as a back-reference to this document.

## Parameters

| Key | Default | Unit | Description |
|-----|---------|------|-------------|
| `full_cycles` | 20 | count | Number of full open/start/capture/close cycles |
| `rapid_cycles` | 50 | count | Number of rapid start/capture/stop cycles |
| `full_warmup` | 3 | count | Warmup frames per full cycle |
| `full_captures` | 5 | count | Frames to capture per full cycle |
| `rapid_pacing_ms` | 10 | ms | Delay between rapid cycles |

## Output Metrics

| Key | Unit | Description |
|-----|------|-------------|
| `full_cycles_success` | count | Full cycles that completed without error |
| `full_cycle_failures` | count | Full cycles that failed |
| `rapid_cycles_ok` | count | Rapid cycles where a frame was captured |
| `rapid_cycles_total` | count | Total rapid cycles attempted |
| `first_frame_latency_mean` | ms | Mean first-frame latency across full cycles |
| `first_frame_latency_stddev` | ms | Std-dev of first-frame latency |
| `first_frame_latency_min` | ms | Minimum first-frame latency |
| `first_frame_latency_max` | ms | Maximum first-frame latency |
| `first_frame_latency_p95` | ms | 95th percentile first-frame latency |
| `first_frame_latency_jitter` | ms | Jitter of first-frame latency |

## Report Details

Summary line format:
```
Full: 20/20 OK. Rapid: 48/50 captured.
```

## Verdict Logic

| Status | Condition |
|--------|-----------|
| **Pass** | full_failures ≤ 0 AND rapid success % ≥ 90% |
| **Warn** | full_failures ≤ 2 AND rapid success % ≥ 70% |
| **Fail** | full_failures > 2 OR rapid success % < 70% |

## Interpretation Guide

- `full_cycle_failures = 0`: Driver handles repeated open/close cleanly with no leaks or state issues.
- `full_cycle_failures > 0`: Possible resource exhaustion or driver state corruption after repeated cycling.
- Low `rapid_cycles_ok`: The driver cannot recover quickly from STREAMOFF; it may need more settling time.
- High `first_frame_latency_max` vs mean: The first frame after STREAMON is occasionally slow — pipeline initialization cost varies.
- Increasing first-frame latency over cycles: Possible memory leak or resource exhaustion in the driver.

## Failure Modes

| Symptom | Likely Cause |
|---------|--------------|
| Increasing full_cycle_failures | Kernel resource leak — buffers or file descriptors not released properly |
| rapid_cycles_ok ≪ rapid_cycles_total | Driver needs >10ms between STREAMOFF and the next open/start |
| first_frame_latency degrades over time | Memory fragmentation or DMA channel exhaustion |
| All rapid cycles fail | Driver cannot start streaming without a longer settle period |
