# t23 — Latency Under Load

**Layer:** 7 — Stability  
**Category:** stability  
**Trigger required:** yes  

## Purpose

Measures how CPU contention affects camera capture latency by comparing a baseline capture session (idle system) against a session performed while synthetic CPU-stress threads saturate all available cores. This test quantifies the system's ability to maintain real-time camera response under adverse scheduling conditions — critical for embedded deployments where the camera shares a processor with compute-heavy workloads.

## How It Works

1. **Baseline phase:**
   - Opens a V4L2 session with 2 buffers, warms up, and captures `sample_count` frames.
   - Records latency of each successful capture.
   - Closes the session.
2. **Load phase:**
   - Spawns `load_threads` CPU-bound threads (XorShift tight loops).
   - Opens a fresh V4L2 session, warms up, and captures `sample_count` frames under full CPU saturation.
   - Records latencies.
   - Signals threads to stop and joins them.
3. **Analysis:**
   - Computes full statistics for both baseline and under-load latency distributions.
   - Calculates `delta_mean_ms` and `delta_p95_ms` (load − baseline).
   - Applies verdict based on `delta_p95_ms`.

## Implementation

Function: `run_latency_under_load` in [diagnostic_runner.cpp](../../../source/backend/core/src/diagnostic_runner.cpp)  
Registry: `t23-latency-under-load` in [test_registry.cpp](../../../source/backend/core/src/test_registry.cpp)

> The source file contains `// Docs: docs/backend/tests/t23-latency-under-load.md`
> above the function as a back-reference to this document.

## Parameters

| Key | Default | Unit | Description |
|-----|---------|------|-------------|
| `sample_count` | 30 | count | Frames captured in each phase (baseline and load). |
| `load_threads` | 4 | count | Number of CPU-saturating threads during load phase. |
| `baseline_timeout_ms` | 100 | ms | Poll timeout for baseline captures. |
| `load_timeout_ms` | 200 | ms | Poll timeout for under-load captures (extended to accommodate jitter). |
| `sample_interval_ms` | 200 | ms | Delay between consecutive captures in both phases. |

## Output Metrics

### Baseline latency statistics (`baseline_latency_*`)

| Metric Key | Unit | Description |
|-----------|------|-------------|
| `baseline_latency_mean` | ms | Mean latency during idle system. |
| `baseline_latency_stddev` | ms | Standard deviation of baseline latency. |
| `baseline_latency_min` | ms | Minimum baseline latency. |
| `baseline_latency_max` | ms | Maximum baseline latency. |
| `baseline_latency_p95` | ms | 95th percentile baseline latency. |
| `baseline_latency_jitter` | ms | Jitter of baseline latency. |
| `baseline_captures` | count | Number of successful baseline frames. |

### Under-load latency statistics (`load_latency_*`)

| Metric Key | Unit | Description |
|-----------|------|-------------|
| `load_latency_mean` | ms | Mean latency under CPU stress. |
| `load_latency_stddev` | ms | Standard deviation of load latency. |
| `load_latency_min` | ms | Minimum load latency. |
| `load_latency_max` | ms | Maximum load latency. |
| `load_latency_p95` | ms | 95th percentile load latency. |
| `load_latency_jitter` | ms | Jitter of load latency. |
| `load_captures` | count | Number of successful under-load frames. |

### Delta metrics

| Metric Key | Unit | Description |
|-----------|------|-------------|
| `delta_mean_ms` | ms | `load_latency_mean − baseline_latency_mean`. |
| `delta_p95_ms` | ms | `load_latency_p95 − baseline_latency_p95`. The primary verdict metric. |

## Report Details

No explicit detail lines are pushed. The summary message reports the p95 delta and verdict.

## Verdict Logic

| Status | Condition |
|--------|-----------|
| **Pass** | `delta_p95_ms < pass_delta_p95_ms` |
| **Warn** | `delta_p95_ms < warn_delta_p95_ms` |
| **Fail** | `delta_p95_ms ≥ warn_delta_p95_ms` |
| **Warn** | No under-load frames captured (load phase failed entirely). |

Default thresholds:

| Threshold | Default |
|-----------|---------|
| `pass_delta_p95_ms` | 5.0 |
| `warn_delta_p95_ms` | 20.0 |

## Interpretation Guide

- **`delta_p95_ms` < 2 ms** — negligible CPU-load impact; the camera driver and DMA path are well-isolated from CPU scheduling.
- **`delta_p95_ms` 2–5 ms** — minor impact; the kernel's buffer management adds some latency under contention but remains within real-time budgets.
- **`delta_p95_ms` 5–20 ms** — moderate impact; the system is usable but may miss deadlines in tight pipelines. Consider CPU affinity or RT scheduling.
- **`delta_p95_ms` > 20 ms** — severe degradation; the capture path shares resources (interrupts, cache, memory bus) with application threads in a way that cripples latency under load.
- **`load_captures` significantly less than `sample_count`** — many captures timed out under load. The system cannot even complete DQBUF within the extended timeout.
- **Negative `delta_mean_ms`** — rare; may occur if baseline captures include initial warm-up overhead that dissipates by the time load runs.

## Failure Modes

| Symptom | Likely Cause |
|---------|--------------|
| `delta_p95_ms` > 20 ms | Camera interrupt handler or DMA completion shares CPU with stress threads. Pin camera IRQ to a dedicated core or raise its priority. |
| `load_captures = 0` | Capture thread starved entirely; all polls timed out. Increase `load_timeout_ms` or assign the capture thread RT priority (`SCHED_FIFO`). |
| `baseline_latency` already high (> 30 ms) | Pipeline overhead even without load; the delta may look small but absolute latency is problematic. Address baseline first. |
| Large `load_latency_jitter` with modest mean | Scheduling contention causes occasional spikes; p95 may be fine but p99 (not reported) is very high. Consider dedicated CPU cores. |
| `delta_p95_ms` very different across runs | Non-deterministic load environment; ensure no other processes compete during test. Run multiple times and average. |
