# t14 — NON_BLOCK vs BLOCK Comparison

**Layer:** 5 — Latency  
**Category:** io-mode  
**Trigger required:** yes  

## Purpose

Compares non-blocking (O_NONBLOCK + spin-poll) and blocking (O_NONBLOCK cleared + blocking DQBUF) capture modes to quantify the latency trade-off. Non-blocking mode can achieve lower latency by avoiding the kernel sleep/wake cycle, but at the cost of CPU spin. This test measures both approaches under identical conditions to help choose the appropriate I/O strategy for the application's latency and CPU budget.

## How It Works

1. **Non-blocking phase:**
   a. Opens the device with `O_NONBLOCK` (default), starts streaming with 2 buffers, and warms up.
   b. For each of `sample_count` samples: drains stale buffers, sends a trigger, then spin-polls `VIDIOC_DQBUF` until success or a 100 ms deadline expires. Records latency and spin count.
   c. Closes the session.

2. **Blocking phase:**
   a. Opens the device, clears `O_NONBLOCK` via `fcntl`, starts streaming with 2 buffers, and warms up.
   b. For each of `sample_count` samples: drains stale buffers, sends a trigger, then issues a blocking `VIDIOC_DQBUF` (which sleeps in the kernel until a buffer is ready). Records latency.
   c. Closes the session.

3. Computes full statistics for both modes and reports the EAGAIN spin count as a CPU cost indicator.

## Implementation

Function: `run_nonblock_vs_block` in [diagnostic_runner.cpp](../../../source/backend/core/src/diagnostic_runner.cpp)  
Registry: `t14-nonblock-vs-block` in [test_registry.cpp](../../../source/backend/core/src/test_registry.cpp)

> The source file contains `// Docs: docs/backend/tests/t14-nonblock-vs-block.md`
> above the function as a back-reference to this document.

## Parameters

| Key | Default | Unit | Description |
|-----|---------|------|-------------|
| `sample_count` | 30 | count | Number of captures per mode |
| `spin_deadline_ms` | 100 | ms | Maximum spin time in non-blocking mode before giving up |
| `poll_timeout_ms` | 200 | ms | Poll timeout used for blocking mode warmup |
| `sample_interval_ms` | 200 | ms | Delay between consecutive samples |

## Output Metrics

### Non-blocking mode metrics

| Metric Key | Unit | Description |
|-----------|------|-------------|
| `nonblock_latency_mean` | ms | Mean latency in non-blocking spin mode |
| `nonblock_latency_stddev` | ms | Standard deviation |
| `nonblock_latency_min` | ms | Minimum latency |
| `nonblock_latency_max` | ms | Maximum latency |
| `nonblock_latency_p95` | ms | 95th percentile latency |
| `nonblock_latency_jitter` | ms | Jitter (max - min) |
| `avg_eagain_spins` | count | Average number of EAGAIN returns before success per frame |
| `nonblock_captures` | count | Total successful non-blocking captures |

### Blocking mode metrics

| Metric Key | Unit | Description |
|-----------|------|-------------|
| `block_latency_mean` | ms | Mean latency in blocking DQBUF mode |
| `block_latency_stddev` | ms | Standard deviation |
| `block_latency_min` | ms | Minimum latency |
| `block_latency_max` | ms | Maximum latency |
| `block_latency_p95` | ms | 95th percentile latency |
| `block_latency_jitter` | ms | Jitter (max - min) |
| `block_captures` | count | Total successful blocking captures |

## Report Details

No explicit detail lines are emitted. The summary states capture success counts for both modes.

## Verdict Logic

| Status | Condition |
|--------|-----------|
| **Pass** | At least one frame captured in either mode |
| **Fail** | No frames captured in either mode |

This test is informational — it does not apply thresholds on the latency difference between modes.

## Interpretation Guide

- **nonblock_latency_mean < block_latency_mean**: Non-blocking spin achieves lower latency (typical on systems with fast trigger-to-readout).
- **block_latency_mean ≈ nonblock_latency_mean**: Kernel wake latency is negligible — blocking mode is preferred (saves CPU).
- **avg_eagain_spins very high (> 10000)**: Significant CPU cost for the non-blocking approach; reconsider using blocking mode with poll().
- **avg_eagain_spins very low (< 100)**: Frame is ready almost immediately after trigger — minimal spin cost.
- **nonblock_captures < sample_count**: Some non-blocking attempts hit the 100 ms deadline — trigger delivery may be inconsistent.
- **block_captures < sample_count**: Blocking DQBUF sometimes fails — possible driver issue with blocking mode.

## Failure Modes

| Symptom | Likely Cause |
|---------|--------------|
| "NON_BLOCK session failed" | Device busy or cannot allocate buffers |
| "BLOCK session open/start failed" | Device state not properly reset between phases |
| No frames in non-blocking mode | Trigger not firing; spin deadline too short for actual pipeline latency |
| No frames in blocking mode | Driver's blocking DQBUF implementation hangs or has a bug |
| Large discrepancy between modes | Kernel scheduling adds measurable overhead in blocking path |
