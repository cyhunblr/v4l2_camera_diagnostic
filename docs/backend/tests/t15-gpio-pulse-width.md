# t15 — GPIO Pulse Width Characterization

**Layer:** 5 — Latency  
**Category:** trigger  
**Trigger required:** yes (hardware only)  

## Purpose

Sweeps GPIO trigger pulse widths from 1 ms to 30 ms to characterize the camera's trigger edge sensitivity. By measuring latency relative to both the rising edge (HIGH) and falling edge (LOW) of the pulse, the test infers whether the camera triggers on the rising edge, falling edge, or is level-sensitive. This information is critical for setting the optimal pulse width in production and understanding minimum pulse requirements.

## How It Works

1. Opens the camera device, starts streaming with 2 buffers, and warms up with 5 captures.
2. For each of 11 pulse widths (1, 2, 3, 5, 7, 10, 13, 15, 20, 25, 30 ms):
   a. For each of `samples_per_width` repetitions:
      - Drains stale buffers and waits briefly.
      - Sends a GPIO trigger pulse of the specified width, recording the HIGH edge time.
      - Computes the LOW edge time as HIGH + pulse_width.
      - Polls for the resulting frame (500 ms timeout).
      - On success, records latency from HIGH edge and latency from LOW edge.
   b. Computes per-width statistics: hit count, mean latency from HIGH, mean latency from LOW.
3. Across all widths that achieved 100% hits, computes the average within-level range for both HIGH-referenced and LOW-referenced latencies.
4. Determines the likely trigger edge:
   - If HIGH range < LOW range − 1 ms → **rising edge** trigger.
   - If LOW range < HIGH range − 1 ms → **falling edge** trigger.
   - Otherwise → **inconclusive**.

## Implementation

Function: `run_gpio_pulse_width` in [diagnostic_runner.cpp](../../../source/backend/core/src/diagnostic_runner.cpp)  
Registry: `t15-gpio-pulse-width` in [test_registry.cpp](../../../source/backend/core/src/test_registry.cpp)

> The source file contains `// Docs: docs/backend/tests/t15-gpio-pulse-width.md`
> above the function as a back-reference to this document.

## Parameters

| Key | Default | Unit | Description |
| ----- | --------- | ------ | ------------- |
| `samples_per_width` | 8 | count | Captures per pulse width level |
| `warmup_count` | 5 | count | Warmup captures before sweep |
| `poll_timeout_ms` | 500 | ms | Maximum wait for frame after trigger |

## Output Metrics

### Per-width metrics (for each pulse width N ∈ {1, 2, 3, 5, 7, 10, 13, 15, 20, 25, 30})

| Metric Key Pattern | Unit | Description |
| ------------------- | ------ | ------------- |
| `hits_{N}ms` | count | Number of successful captures at width N ms |
| `lat_high_avg_{N}ms` | ms | Mean latency measured from the rising (HIGH) edge |
| `lat_low_avg_{N}ms` | ms | Mean latency measured from the falling (LOW) edge |

### Aggregate metrics (only when full-hit widths exist)

| Metric Key | Unit | Description |
| ----------- | ------ | ------------- |
| `range_h` | ms | Average within-level range of HIGH-referenced latency |
| `range_l` | ms | Average within-level range of LOW-referenced latency |

## Report Details

```text
1ms: hits=8/8 lat_HIGH=34ms lat_LOW=33ms
2ms: hits=8/8 lat_HIGH=34ms lat_LOW=32ms
3ms: hits=8/8 lat_HIGH=35ms lat_LOW=32ms
5ms: hits=7/8 lat_HIGH=34ms lat_LOW=29ms
...
30ms: hits=8/8 lat_HIGH=33ms lat_LOW=3ms
Edge detection: rising
```

## Verdict Logic

| Status | Condition |
| ------ | --------- |
| **Pass** | Sweep completes (regardless of edge detection result) |
| **Fail** | Session setup fails |

This test always passes if it can run — it is a characterization test. The edge detection result is informational.

## Interpretation Guide

- **Edge = rising**: Camera triggers on the rising edge of the pulse. The minimum pulse width is the smallest width with 100% hits.
- **Edge = falling**: Camera triggers on the falling edge. Latency from LOW is more consistent than from HIGH.
- **Edge = inconclusive**: Camera may be level-sensitive, or the measurement noise exceeds the edge timing difference.
- **hits < samples_per_width at narrow widths**: The pulse is too short for the camera's trigger input filter — this is the minimum viable pulse width.
- **lat_HIGH constant across all widths, lat_LOW decreasing**: Classic rising-edge trigger behavior — latency from trigger to frame is fixed regardless of pulse duration.
- **range_h ≈ range_l**: Both edges contribute to latency equally — level-sensitive or center-triggered.

## Failure Modes

| Symptom | Likely Cause |
| --------- | -------------- |
| "Session setup failed" | GPIO trigger hardware not connected or device busy |
| 0 hits at all widths | Trigger pin not connected, wrong GPIO line, or camera not in external trigger mode |
| Inconsistent hit counts | Electrical noise on the trigger line, or pulse too close to the minimum filter threshold |
| Edge always inconclusive | Camera uses a long debounce filter that masks edge timing differences |
