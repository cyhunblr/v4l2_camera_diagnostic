# t03 — Pipeline Readiness After STREAMON

**Layer:** 2 — State-machine correctness  
**Category:** stream-state  
**Trigger modes:** Hardware | Software | FreeRun  

## Purpose

Measures how long the capture pipeline takes to deliver its **first frame after `VIDIOC_STREAMON` returns**, by spinning on `VIDIOC_DQBUF` rather than waiting on `poll()`.

`poll()` reports when the kernel says a buffer is ready, which hides how long the pipeline itself needed to produce one. Spinning on `DQBUF` and counting `EAGAIN` measures that directly.

The test also times `VIDIOC_STREAMON` itself. On hardware where several sensors sit behind a shared deserializer and fsync source, a single STREAMON re-initialises the *whole camera group* over I2C and can take seconds instead of milliseconds. That is a significant finding on its own, and this test surfaces it without the repeated stream cycling that makes [t06](t06-stream-cycles.md) unsafe on such hardware.

## How It Works

1. Opens the device and allocates buffers with `setup_buffers()`. `V4lSession::start()` bundles buffer setup with STREAMON; the two are called separately here so the measurement starts exactly when STREAMON returns, with allocation left outside it.
2. Times `streamon()` → `streamon_ms`.
3. Takes `t0` immediately after STREAMON returns, fires one trigger pulse, then spins:
   - Issues `VIDIOC_DQBUF` on the already non-blocking fd, counting `EAGAIN` returns.
   - Every `trigger_retry_ms`, fires another pulse. Under a hardware or software trigger the sensor emits nothing until a pulse arrives, and a pulse fired before the pipeline was ready is simply lost — without retrying, the cycle would always time out. Under free-run `send_async()` is a no-op timestamp, so the same loop works unchanged.
   - On the first successful `DQBUF`, records `first_frame_ms` and requeues the buffer.
4. Repeats for `cycles` iterations, waiting `settle_ms` between them.
5. **Stops early** if any STREAMON exceeds `slow_start_ms`. One slow measurement already answers the question, and repeating it on hardware that re-initialises a camera group per stream start carries real risk.

## Implementation

Function: `run_pipeline_ready` in [diagnostic_runner.cpp](../../../source/backend/core/src/diagnostic_runner.cpp)  
Registry: `t03-pipeline-ready` in [test_registry.cpp](../../../source/backend/core/src/test_registry.cpp)

> The source file contains `// Docs: docs/backend/tests/t03-pipeline-ready.md`
> above the function as a back-reference to this document.

`buf.memory` is filled from `V4lSession::memory_type()` rather than hard-coded to `V4L2_MEMORY_MMAP`, and the buffer is returned with the public `requeue(index)` so USERPTR buffers get their `m.userptr` restored.

## Parameters

| Key | Default | Unit | Description |
| ----- | --------- | ------ | ------------- |
| `buffer_count` | 2 | count | Buffers to allocate. Does not affect the measurement, which begins after STREAMON |
| `first_frame_deadline_ms` | 3000 | ms | Upper bound on the spin. Without it the loop never returns when no frame arrives |
| `trigger_retry_ms` | 100 | ms | Interval between trigger pulses while waiting. Required under hardware/software triggering |
| `cycles` | 3 | count | Measurement rounds |
| `settle_ms` | 500 | ms | Pause between rounds |
| `slow_start_ms` | 2000 | ms | A STREAMON slower than this stops the remaining rounds. Zero or negative disables the watchdog |

## Output Metrics

| Key | Unit | Description |
| ----- | ------ | ------------- |
| `first_frame_ms_mean` / `_max` | ms | Time from STREAMON returning to the first frame — the primary result |
| `streamon_ms_mean` / `_max` | ms | Duration of `VIDIOC_STREAMON` itself |
| `streamon_attempts_max` | count | Most STREAMON attempts needed in a cycle |
| `trigger_pulses_to_first_frame` | count | Mean pulses spent before the first frame arrived |
| `eagain_spins_to_first_frame` | count | Mean `EAGAIN` spins on DQBUF before the first frame |
| `cycles_completed` | count | Rounds actually measured; below `cycles` if the watchdog stopped early |
| `first_frame_timeouts` | count | Rounds where no frame arrived within the deadline |

> `streamon()` retries up to 3 times with a 50 ms backoff on transient I2C errors, so `streamon_ms` can include up to ~100 ms of sleep. Check `streamon_attempts_max` before reading anything into a slightly inflated figure.

## Report Details

One detail line per cycle, plus a line when the watchdog stops the run:

```text
cycle 1: STREAMON=42ms, first frame=147ms, pulses=2
cycle 2: STREAMON=39ms, first frame=145ms, pulses=2
cycle 3: STREAMON=41ms, first frame=146ms, pulses=2
```

## Verdict Logic

| Status | Condition |
| -------- | ----------- |
| **Fail** | Session setup or STREAMON failed, or no frame arrived in any cycle |
| **Warn** | `streamon_ms_max > slow_start_ms` — checked first |
| **Warn** | Some cycles timed out, or `first_frame_ms_mean > pass_first_frame_ms` |
| **Pass** | `first_frame_ms_mean <= pass_first_frame_ms` |

The slow-STREAMON check deliberately precedes the latency verdict. Once the pipeline is up the first frame can still arrive quickly, so scoring latency first would let a multi-second STREAMON pass unreported — the very finding this test exists to surface.

**Thresholds from `default_threshold_config`:**

| Key | Default | Description |
| --- | ------- | ----------- |
| `pass_first_frame_ms` | 500.0 | Mean first-frame time at or below which the test passes |
| `warn_first_frame_ms` | 1500.0 | Above this the pipeline is called slow to deliver |

## Interpretation Guide

- **`first_frame_ms` and `streamon_ms` measure different things.** The first is pipeline readiness; the second is how long the driver took to bring the stream up. A device can be fast at one and slow at the other.
- **`streamon_ms` in milliseconds, `first_frame_ms` ≈ sensor latency + one retry interval** — healthy. With a ~45 ms sensor and `trigger_retry_ms=100`, expect roughly 145 ms.
- **`streamon_ms` in seconds** — every stream start is re-initialising a shared camera group over I2C. Cross-check the kernel log for capture-channel timeouts and sensor power-on messages. This also explains why [t06](t06-stream-cycles.md) is gated as experimental on such hardware.
- **`trigger_pulses_to_first_frame` > 1** — the pipeline discarded early pulses; consistent with the warm-up frame [t26](t26-cold-start.md) reports.
- **`eagain_spins_to_first_frame` very high with normal `first_frame_ms`** — expected. The spin is a busy loop; the count reflects CPU speed, not pipeline health.

## Failure Modes

| Symptom | Likely Cause |
| --------- | -------------- |
| No frame in any cycle | Trigger not wired or not firing, sensor not streaming, or `trigger_retry_ms` longer than the deadline |
| `streamon_ms` in seconds | Shared deserializer/fsync group re-initialised per stream start; driver or hardware issue, not a tuning problem |
| `streamon_attempts_max` > 1 | Transient I2C errors on stream start; the driver retried and succeeded |
| `first_frame_ms` near the deadline | Pipeline very slow to produce, or pulses are being lost faster than they are retried |
