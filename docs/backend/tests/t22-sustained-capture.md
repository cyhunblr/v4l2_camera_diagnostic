# t22 — Sustained Capture

**Layer:** 7 — Stability  
**Category:** stability  
**Trigger required:** yes  

## Purpose

Measures long-duration capture reliability by continuously acquiring frames over a configurable window (default 60 seconds) and tracking success rate, latency drift, and per-window statistics. This test reveals thermal degradation, memory leaks, driver timeout escalation, and any condition that worsens over time — issues invisible in short burst tests.

## How It Works

1. Opens a V4L2 session with 2 buffers and warms up.
2. Enters a timed capture loop for `duration_sec` seconds, sampling at `sample_interval_ms` intervals.
3. Divides the run into windows of `window_sec` seconds each. Within each window, accumulates latency measurements and miss counts.
4. At each window boundary, computes and records per-window statistics (mean, stddev, miss count).
5. After the full duration completes, computes:
   - Overall success rate.
   - Latency drift: difference between the mean of the second-half windows and the mean of the first-half windows.
6. Reports aggregate latency statistics, per-window detail lines, and drift metric.
7. Applies a two-axis verdict on both success rate and latency drift.

## Implementation

Function: `run_sustained_capture` in [diagnostic_runner.cpp](../../../source/backend/core/src/diagnostic_runner.cpp)  
Registry: `t22-sustained-capture` in [test_registry.cpp](../../../source/backend/core/src/test_registry.cpp)

> The source file contains `// Docs: docs/backend/tests/t22-sustained-capture.md`
> above the function as a back-reference to this document.

## Parameters

| Key | Default | Unit | Description |
| ----- | --------- | ------ | ------------- |
| `duration_sec` | 60 | s | Total test duration. |
| `window_sec` | 10 | s | Length of each statistical window. |
| `sample_interval_ms` | 100 | ms | Delay between capture attempts (≈ 10 Hz). |
| `capture_timeout_ms` | 100 | ms | Per-frame poll timeout. |

## Output Metrics

### Aggregate metrics

| Metric Key | Unit | Description |
| ----------- | ------ | ------------- |
| `frames_captured` | count | Total successful frames over the entire duration. |
| `success_rate_pct` | % | `frames_captured / (frames_captured + misses) × 100`. |
| `max_consecutive_miss` | count | Longest streak of consecutive failed captures. |
| `latency_drift_ms` | ms | Mean latency of second-half windows minus mean of first-half windows. Positive = degradation. |

### Latency statistics (`latency_*`)

| Metric Key | Unit | Description |
| ----------- | ------ | ------------- |
| `latency_mean` | ms | Mean capture latency across all successful frames. |
| `latency_stddev` | ms | Standard deviation of capture latency. |
| `latency_min` | ms | Minimum capture latency observed. |
| `latency_max` | ms | Maximum capture latency observed. |
| `latency_p95` | ms | 95th percentile capture latency. |
| `latency_jitter` | ms | Jitter (max − min) of capture latency. |

## Report Details

Per-window lines with the format:

```text
Win0 0-10s: n=98 mean=12ms stddev=3 miss=2
Win1 10-20s: n=100 mean=11ms stddev=2 miss=0
Win2 20-30s: n=99 mean=13ms stddev=4 miss=1
...
```

Each line shows frame count, mean latency, standard deviation, and miss count for that window.

## Verdict Logic

| Status | Condition |
| -------- | ----------- |
| **Pass** | `success_rate_pct ≥ pass_rate_pct` AND `\|latency_drift_ms\| < pass_drift_ms` |
| **Warn** | `success_rate_pct ≥ warn_rate_pct` AND `\|latency_drift_ms\| < warn_drift_ms` |
| **Fail** | `success_rate_pct < warn_rate_pct` OR `\|latency_drift_ms\| ≥ warn_drift_ms` |

Default thresholds:

| Threshold | Default |
| ----------- | --------- |
| `pass_rate_pct` | 95.0 |
| `warn_rate_pct` | 80.0 |
| `pass_drift_ms` | 1.0 |
| `warn_drift_ms` | 5.0 |

## Interpretation Guide

- **`success_rate_pct` > 99%** — excellent pipeline reliability; suitable for production frame-critical applications.
- **`success_rate_pct` 95–99%** — acceptable; occasional misses are typically due to scheduling jitter.
- **`success_rate_pct` < 80%** — serious reliability issue; significant frame loss under sustained load.
- **`latency_drift_ms` ≈ 0** — stable performance over time; no thermal or resource degradation.
- **`latency_drift_ms` > 0 (positive)** — latency increases over time. Common causes: thermal throttling, memory pressure, driver internal queue growth.
- **`latency_drift_ms` < 0 (negative)** — latency improves over time. Usually indicates initial warm-up effect captured in the first window.
- **`max_consecutive_miss` > 5** — a significant gap in frame delivery; may indicate momentary hardware stall or USB disconnect.
- **Per-window means increasing monotonically** — clear thermal degradation signature.

## Failure Modes

| Symptom | Likely Cause |
| --------- | -------------- |
| High success rate but `latency_drift_ms` exceeds threshold | Thermal throttling — sensor or SoC reducing clock speed under sustained load. Add heatsink or reduce ambient temperature. |
| Low success rate across all windows uniformly | Trigger timing mismatch or insufficient USB bandwidth for sustained rate. |
| Success rate degrades in later windows | Memory leak or driver resource exhaustion — buffers not being recycled correctly over time. |
| `max_consecutive_miss` very high in one window, others fine | Transient system event (GC, cron job, USB bus reset). Check system logs around that timestamp. |
| All windows show high stddev | Inherent scheduling jitter — consider using `SCHED_FIFO` for the capture thread or dedicating a CPU core. |
