# t24 — Max FPS

**Layer:** 7 — Stability  
**Category:** stability  
**Trigger required:** yes  

## Purpose

Determines the maximum sustained frame rate the camera pipeline can deliver by triggering and capturing frames as fast as possible over a fixed duration. Unlike controlled-rate tests that pace captures at a known interval, this test runs flat-out to find the upper throughput limit — revealing the hardware's true ceiling and quantifying the drop rate when driven at maximum speed.

## How It Works

1. Opens a V4L2 session with **4 buffers** (more than other tests) to maximise pipeline depth.
2. Performs a warmup of `warmup_frames` triggers with no pacing.
3. Enters a tight loop for `duration_sec` seconds:
   - Sends a trigger.
   - Attempts capture with `poll_timeout_ms` timeout.
   - Records success/miss and latency.
   - Tracks per-second window frame counts to find peak instantaneous rate.
4. After the duration, computes:
   - **Sustained FPS** — total received frames / total elapsed time.
   - **Max window FPS** — peak frame rate in any 1-second window.
   - **Send rate** — triggers sent per second (the demanded rate).
   - **Drop percentage** — missed / sent × 100.
5. Reports latency statistics over all successful captures.
6. Applies verdict based on the drop percentage.

## Implementation

Function: `run_max_fps` in [diagnostic_runner.cpp](../../../source/backend/core/src/diagnostic_runner.cpp)  
Registry: `t24-max-fps` in [test_registry.cpp](../../../source/backend/core/src/test_registry.cpp)

> The source file contains `// Docs: docs/backend/tests/t24-max-fps.md`
> above the function as a back-reference to this document.

## Parameters

| Key | Default | Unit | Description |
|-----|---------|------|-------------|
| `duration_sec` | 10 | s | Total timed capture duration. |
| `warmup_frames` | 20 | count | Frames captured during warmup (no timing). |
| `poll_timeout_ms` | 100 | ms | Per-capture poll timeout. |

## Output Metrics

### Rate metrics

| Metric Key | Unit | Description |
|-----------|------|-------------|
| `sustained_fps` | fps | Total frames received / elapsed seconds. The headline throughput number. |
| `max_window_fps` | fps | Peak frame rate observed in any 1-second sliding window. |
| `send_rate` | fps | Trigger sends per second achieved. |
| `total_sent` | count | Total triggers sent during the timed window. |
| `total_received` | count | Total successful frames dequeued. |
| `total_missed` | count | Total captures that timed out. |
| `drop_pct` | % | `total_missed / total_sent × 100`. |

### Latency statistics (`latency_*`)

| Metric Key | Unit | Description |
|-----------|------|-------------|
| `latency_mean` | ms | Mean capture latency for successful frames. |
| `latency_stddev` | ms | Standard deviation of capture latency. |
| `latency_min` | ms | Minimum capture latency. |
| `latency_max` | ms | Maximum capture latency. |
| `latency_p95` | ms | 95th percentile capture latency. |
| `latency_jitter` | ms | Jitter of capture latency. |

## Report Details

```
Duration: 10s
Sustained FPS: 28
Peak window FPS: 31
Drop rate: 3%
```

## Verdict Logic

| Status | Condition |
|--------|-----------|
| **Pass** | `drop_pct < 5%` |
| **Warn** | `drop_pct < 20%` |
| **Fail** | `drop_pct ≥ 20%` |

## Interpretation Guide

- **`sustained_fps` close to sensor's native rate** (e.g. 30 fps for a 30 fps sensor) — the pipeline keeps up with the hardware; no bottleneck.
- **`sustained_fps` well below native rate** with low `drop_pct` — the trigger/capture loop itself is the bottleneck (e.g. DQBUF latency plus the tight loop overhead limits throughput).
- **High `drop_pct`** — the pipeline is being driven faster than it can sustain. Frames are requested but never delivered within the timeout.
- **`max_window_fps` > `sustained_fps`** — burst capability exceeds steady-state; the camera can briefly exceed its average rate (often the first second after warmup).
- **`send_rate` >> `sustained_fps`** — the test is trigger-limited rather than sensor-limited; consider increasing buffer count or using a mode that doesn't require explicit trigger per frame.
- **`latency_mean` low but `latency_p95` high** — most frames are fast but occasional outliers (DMA scheduling, interrupt coalescing) drag down throughput.

## Failure Modes

| Symptom | Likely Cause |
|---------|--------------|
| `drop_pct` > 20% | Trigger rate exceeds what the sensor + driver can deliver. The sensor's maximum frame rate is lower than the test's trigger rate. |
| `sustained_fps` far below expected | USB bandwidth saturation (especially with high-resolution uncompressed); try a lower resolution or MJPEG. |
| `max_window_fps` = 0 or very low | Camera not responding to triggers at all; verify trigger wiring and that the sensor is in triggered (external sync) mode. |
| High `latency_jitter` with moderate FPS | Buffer starvation causing variable wait times; increase buffer count (already 4 in this test, but driver may need more). |
| `total_sent` much higher than expected for duration | The trigger-send + capture loop completes very quickly when captures fail, inflating the send count. Check `total_missed`. |
