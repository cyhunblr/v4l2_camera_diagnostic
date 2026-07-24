# t13 — Trigger to DQBUF Latency

**Layer:** 5 — Latency  
**Category:** latency  
**Trigger required:** yes  

## Purpose

Measures the end-to-end latency from trigger signal emission to frame dequeue (DQBUF). This is the fundamental latency metric for triggered camera systems — it captures the full pipeline delay including sensor integration, readout, DMA transfer, and kernel buffer management. The statistical distribution of this latency informs real-time system design and timeout configuration.

## How It Works

1. Opens the camera device and starts streaming with 2 buffers.
2. Warms up the pipeline with 5 trigger/capture cycles to stabilize sensor and ISP state.
3. For each of `sample_count` iterations:
   a. Sends a trigger signal and records the timestamp.
   b. Attempts capture with a 100 ms timeout.
   c. On success, records the latency (time from trigger to successful DQBUF).
   d. On failure, increments the miss counter.
   e. Waits 200 ms between samples.
4. Computes statistics (mean, stddev, min, max, p95, jitter) on all successful latency samples.

## Implementation

Function: `run_trigger_latency` in [diagnostic_runner.cpp](../../../source/backend/core/src/diagnostic_runner.cpp)  
Registry: `t13-trigger-latency` in [test_registry.cpp](../../../source/backend/core/src/test_registry.cpp)

> The source file contains `// Docs: docs/backend/tests/t13-trigger-latency.md`
> above the function as a back-reference to this document.

## Parameters

| Key | Default | Unit | Description |
|-----|---------|------|-------------|
| `sample_count` | 50 | count | Number of trigger/capture attempts |
| `warmup_count` | 5 | count | Warmup captures before measurement |
| `capture_timeout_ms` | 100 | ms | Poll timeout per capture attempt |
| `sample_interval_ms` | 200 | ms | Delay between consecutive samples |

## Output Metrics

| Metric Key | Unit | Description |
|-----------|------|-------------|
| `frames_captured` | count | Number of successful captures |
| `frames_missed` | count | Number of timed-out capture attempts |
| `latency_mean` | ms | Mean trigger-to-DQBUF latency |
| `latency_stddev` | ms | Standard deviation of latency |
| `latency_min` | ms | Minimum observed latency |
| `latency_max` | ms | Maximum observed latency |
| `latency_p95` | ms | 95th percentile latency |
| `latency_jitter` | ms | Jitter (max - min) of latency |

## Report Details

No explicit detail lines are emitted beyond the summary. The log output includes per-sample progress and a full histogram:

```
Sample 10/50: lat=33ms (running mean=34 stddev=2ms)
Sample 20/50: lat=35ms (running mean=34 stddev=3ms)
...
```

## Verdict Logic

| Status | Condition |
|--------|-----------|
| **Pass** | At least one frame was captured successfully |
| **Fail** | All capture attempts timed out (zero frames captured) |

This test does not apply warn/fail thresholds on latency values — it is primarily a measurement/characterization test. The captured statistics inform other tests and production configuration.

## Interpretation Guide

- **latency_mean 30–50 ms**: Typical for triggered industrial cameras at standard integration times.
- **latency_p95 close to latency_mean**: Consistent pipeline — low jitter, suitable for real-time applications.
- **latency_jitter > 10 ms**: Significant variability — may indicate ISP processing variation, kernel scheduling delays, or trigger timing drift.
- **frames_missed > 0**: Some triggers did not produce a frame within 100 ms — possible trigger edge issues, sensor busy states, or buffer starvation.
- **latency_min very low (< 5 ms)**: May indicate a stale/pre-buffered frame rather than a fresh capture — check t19 (sequence continuity).
- **latency_stddev increasing with sample count**: Thermal drift or sensor AGC adaptation during the measurement window.

## Failure Modes

| Symptom | Likely Cause |
|---------|--------------|
| All frames missed | Trigger not connected, wrong GPIO pin, or camera not in external trigger mode |
| Very high latency (> 100 ms) | Long integration time configured, or camera in free-run mode ignoring triggers |
| Bimodal distribution | Alternating between fresh captures and stale buffer dequeues |
| Gradually increasing latency | Thermal throttling on sensor or SoC during extended capture |
