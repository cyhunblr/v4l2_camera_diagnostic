# t08 — Buffer Recycling Timing

**Layer:** 3 — Buffer & memory  
**Category:** buffering  
**Trigger required:** yes  

## Purpose

Measures the driver's sensitivity to the delay between DQBUF (dequeue) and QBUF (requeue). With only 2 buffers, any delay in recycling a buffer back to the driver can cause frame misses if the second buffer is consumed before the first is returned. This test finds the "cliff" — the delay at which misses begin.

## How It Works

1. Opens the device and starts streaming with 2 buffers.
2. Warms up the pipeline.
3. For each delay value in {0, 1, 5, 10, 20, 30, 40, 48, 50, 60, 80, 100} ms:
   a. Performs 10 repetitions:
      - Captures a frame (DQBUF) but does NOT immediately requeue it.
      - Sleeps for the specified delay.
      - Requeues the buffer (QBUF).
      - Captures a second frame to confirm the pipeline is still delivering.
   b. Records the hit count (successful second captures) out of 10 reps.
4. Identifies the "cliff delay" — the first delay value where hits < reps.

## Implementation

Function: `run_buffer_recycling` in [diagnostic_runner.cpp](../../../source/backend/core/src/diagnostic_runner.cpp)  
Registry: `t08-buffer-recycling` in [test_registry.cpp](../../../source/backend/core/src/test_registry.cpp)

> The source file contains `// Docs: docs/backend/tests/t08-buffer-recycling.md`
> above the function as a back-reference to this document.

## Parameters

| Key | Default | Unit | Description |
| ----- | --------- | ------ | ------------- |
| `reps_per_delay` | 10 | count | Repetitions at each delay value |
| `capture_timeout_ms` | 100 | ms | Timeout for each capture attempt |
| `inter_rep_interval_ms` | 100 | ms | Delay between repetitions |

## Output Metrics

| Key | Unit | Description |
| ----- | ------ | ------------- |
| `hits_delay_0ms` | count | Successful second captures at 0ms delay |
| `hits_delay_1ms` | count | Successful second captures at 1ms delay |
| `hits_delay_5ms` | count | Successful second captures at 5ms delay |
| `hits_delay_10ms` | count | Successful second captures at 10ms delay |
| `hits_delay_20ms` | count | Successful second captures at 20ms delay |
| `hits_delay_30ms` | count | Successful second captures at 30ms delay |
| `hits_delay_40ms` | count | Successful second captures at 40ms delay |
| `hits_delay_48ms` | count | Successful second captures at 48ms delay |
| `hits_delay_50ms` | count | Successful second captures at 50ms delay |
| `hits_delay_60ms` | count | Successful second captures at 60ms delay |
| `hits_delay_80ms` | count | Successful second captures at 80ms delay |
| `hits_delay_100ms` | count | Successful second captures at 100ms delay |
| `cliff_delay_ms` | ms | First delay value causing misses (-1 if no cliff found) |

## Report Details

```text
delay=0ms hits=10/10 mean=33ms
delay=1ms hits=10/10 mean=33ms
delay=5ms hits=10/10 mean=34ms
delay=10ms hits=10/10 mean=33ms
delay=20ms hits=10/10 mean=34ms
delay=30ms hits=10/10 mean=33ms
delay=40ms hits=10/10 mean=34ms
delay=48ms hits=9/10 mean=35ms
delay=50ms hits=7/10 mean=36ms
delay=60ms hits=3/10 mean=38ms
delay=80ms hits=0/10
delay=100ms hits=0/10
```

## Verdict Logic

| Status | Condition |
| ------ | --------- |
| **Pass** | No cliff found (cliff_delay = -1) OR cliff_delay ≥ 50ms (min_safe_cliff_delay_ms threshold) |
| **Warn** | cliff_delay < 50ms — application must requeue buffers quickly or risk misses |

## Interpretation Guide

- `cliff_delay_ms = -1`: Excellent — the driver handles any recycling delay up to 100ms without missing frames (rare with 2-buffer setups).
- `cliff_delay_ms ≥ 50ms`: Safe for most applications — you have at least 50ms to process a frame before requeuing.
- `cliff_delay_ms < 50ms`: Applications must minimize processing time between DQBUF and QBUF, or use more buffers.
- `cliff_delay_ms = 0`: Extremely sensitive — even immediate requeue sometimes misses (possible timing race in driver).
- Gradual degradation (e.g., 10/10 → 9/10 → 7/10): Natural cliff behavior with sensor/trigger timing jitter.
- Abrupt cliff (e.g., 10/10 → 0/10): Hard timeout boundary in the driver's buffer management.

## Failure Modes

| Symptom | Likely Cause |
| --------- | -------------- |
| "Session setup failed" | Cannot open device or start streaming |
| `cliff_delay_ms = 0` | Driver has extremely tight buffer timing or a race condition |
| All delays show misses | Trigger or capture path is unreliable (not a recycling issue) |
| hits_delay_0ms < 10 | Baseline capture is unreliable — test results may be misleading |
