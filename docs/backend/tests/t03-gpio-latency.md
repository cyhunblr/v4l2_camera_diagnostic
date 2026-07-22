# t03 — Trigger to DQBUF latency

> - **Implementation:** search `run_t03` in [diagnostic_runner.cpp](../../../source/backend/core/src/diagnostic_runner.cpp)
> - **Definition:** search `t03-gpio-latency` in [test_registry.cpp](../../../source/backend/core/src/test_registry.cpp)
> - **Category:** `latency`
> - **`uses_trigger`:** yes
> - **Trigger modes:** Hardware, Software (no free-run)

## 1. Overview / scope

**What it measures:** trigger→DQBUF latency — time from firing a trigger to
dequeuing the resulting frame with `VIDIOC_DQBUF`.

**Questions it answers:**

- When I trigger the sensor, how long until the frame reaches the application?
- How stable is that delay from one trigger to the next?

**Why it matters:** it is the core timing test for a triggered pipeline. High
or unstable latency points at driver queueing, buffer negotiation, or
trigger-wiring problems.

**Method:** fire a fixed number of triggers back-to-back, record each frame's
latency, summarize the distribution (mean, spread, tail, jitter), and count
missed triggers.

## 2. Trigger modes

Mask `0x01 | 0x02` — **Hardware** and **Software** only.

- **Hardware** — GPIO pulse per sample; latency includes the physical trigger path.
- **Software** — V4L2 control write; latency reflects the control path.
- **FreeRun** — not supported; reported `Skipped`. A trigger→DQBUF measurement needs a trigger.

Both supported modes share the same code path; only the trigger source differs.

## 3. Inputs

All parameters are currently fixed in `run_t03()`:

| Input | Value | Meaning |
| --- | --- | --- |
| Sample count | `50` | Number of triggered frames measured. |
| Warm-up triggers | `5` | Fired before measurement to reach steady state; not recorded. |
| Capture timeout | `100 ms` | Per-sample wait for a frame; exceeding it counts as a miss. |
| Sample interval | `200 ms` | Delay between consecutive triggers. |
| Buffer count | `2` | Buffers requested via `VIDIOC_REQBUFS`. |
| Memory backend | selected run backend | `mmap` / `dmabuf` / `userptr`. |
| Trigger pulse width | `13 ms` (default) | Pulse length passed to the trigger source. |

> **Preconditions:** a streaming-capable camera plus a working trigger for the
> selected mode (a Hardware GPIO channel or a Software control channel from the
> active profile). If the trigger source cannot be opened, the test is
> `Skipped` ("Trigger source is unavailable for mode ...").

## 4. Outputs & interpretation

### Metrics

| Metric | Unit | Meaning |
| --- | --- | --- |
| `frames_captured` | count | Triggers that produced a frame within the timeout. |
| `frames_missed` | count | Triggers that timed out with no frame. |
| `latency_mean` | ms | Average trigger→DQBUF latency. |
| `latency_stddev` | ms | Standard deviation of latency. |
| `latency_min` | ms | Fastest observed latency. |
| `latency_max` | ms | Slowest observed latency. |
| `latency_p95` | ms | 95th-percentile latency (tail behavior). |
| `latency_jitter` | ms | Latency jitter (cycle-to-cycle variation). |

The log also emits a latency stats line and a distribution histogram; these
are human-readable views of the same data and are not stored as metrics.

### Status

| Status | Condition | Meaning |
| --- | --- | --- |
| `Pass` | ≥ 1 frame captured | Measurement completed and produced latency data. |
| `Fail` | all 50 attempts timed out | No frames at all — trigger not reaching the sensor, wrong channel, or a dead capture path. |
| `Error` | session setup failed | Device could not be opened or streaming could not start (see summary). |
| `Skipped` | free-run mode, or trigger source unavailable | Expected — see the Trigger modes and Inputs sections above. |

> **Note:** the runner marks `Pass` as soon as one frame is captured; it does
> **not** currently apply latency thresholds. Judging whether the latency is
> *good* is the reader's job, using the guidance below.

### How to read the numbers (Phase-0 defaults)

Interpret latency relative to your sensor's frame period `T = 1000 / fps` ms
(e.g. 33 ms at 30 fps). These are documentation defaults, not enforced limits:

- **`latency_mean`** — a healthy hardware-triggered path lands near one frame
  period. Roughly: `≤ 1.5 × T` good, `1.5–3 × T` worth investigating,
  `> 3 × T` a problem (excess queueing or a slow trigger path).
- **`latency_p95` vs `latency_mean`** — a p95 close to the mean means tight,
  predictable timing. A p95 far above the mean signals occasional stalls even
  if the average looks fine.
- **`latency_jitter` / `latency_stddev`** — low is good. High jitter with a low
  mean often matters more than a slightly high mean, because it breaks
  synchronization assumptions.
- **`frames_missed`** — expect `0`. Any misses indicate the trigger is not
  reliably producing frames within 100 ms; correlate with GPIO wiring
  (Hardware) or control validity (Software).
- **Hardware vs Software** — Software latency is typically higher and more
  variable than Hardware; a large gap quantifies the cost of control-based
  triggering on your platform.

## 5. How the code works

`run_t03()` follows the shared capture pattern used across the latency tests:

1. **Open + start** — opens the device and starts streaming with `2` buffers
   on the selected backend via the `V4lSession` helper. Failure here yields
   `Error`.
2. **Warm-up** — `s.warmup(trigger)` fires 5 discarded triggers so the first
   measured sample is not skewed by cold-start effects.
3. **Measure loop** — 50 iterations. Each calls `s.capture(trigger, 100)`,
   which fires the trigger and waits up to 100 ms for the frame; on success it
   records `latency_ms`, otherwise it counts a miss. Every 10th sample logs a
   running mean/stddev. Iterations are spaced 200 ms apart.
4. **Aggregate** — pushes `frames_captured` / `frames_missed`, then (if any
   frames arrived) `compute_stats()` over the latencies and
   `push_stats_metrics()` to emit the `latency_*` family. A latency stats line
   and histogram are logged for humans.
5. **Verdict** — `Fail` if the latency set is empty (every attempt timed out),
   otherwise `Pass` with a summary carrying mean and p95.

The measurement is trigger-source-agnostic: `run_t03()` takes a
`TriggerSource&`, so the Hardware and Software modes reuse the exact same code
path and differ only in which trigger implementation is passed in.
