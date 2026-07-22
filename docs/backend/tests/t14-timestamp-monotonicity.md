# t14 — Timestamp monotonicity

> - **Implementation:** search `run_t14` in [diagnostic_runner.cpp](../../../source/backend/core/src/diagnostic_runner.cpp)
> - **Definition:** search `t14-timestamp-monotonicity` in [test_registry.cpp](../../../source/backend/core/src/test_registry.cpp)
> - **Category:** `metadata`
> - **`uses_trigger`:** yes
> - **Trigger modes:** all (Hardware, Software, FreeRun)

## 1. Overview / scope

**What it checks:** whether V4L2 buffer timestamps strictly increase from
frame to frame, and how far the buffer timestamp diverges from the
application's own wall-clock read of when it actually received the frame.

**Questions it answers:**

- Does the buffer timestamp ever go backward or repeat?
- How consistent is the interval between consecutive buffer timestamps
  (i.e. is the frame cadence stable)?
- How far apart are the driver's buffer timestamp and the application's
  wall-clock receive time, and how much does that offset vary?

**Why it matters:** buffer timestamps are what applications use to pace,
synchronize, or reorder frames. A single backward jump breaks that
contract entirely, and this test is the suite's dedicated, statistically
detailed check for it — [t08](t08-sequence-continuity.md) also checks for
non-monotonic timestamps, but only as one signal among several; this test
is where the timestamp delta and wall-clock-offset distributions are
actually characterized.

**Method:** capture 100 triggered frames, record each one's buffer timestamp
and the application's own receive-time timestamp, then compute the
frame-to-frame delta distribution and the wall-clock-vs-buffer offset
distribution.

## 2. Trigger modes

No restriction — timestamp behavior is checked the same way regardless of
trigger source.

## 3. Inputs

All parameters are fixed in `run_t14`:

| Input | Value | Meaning |
| --- | --- | --- |
| Sample count | `100` | Triggered frames captured. |
| Buffer count | `2` | |
| Capture timeout | `100 ms` | Per sample. |
| Sample interval | `100 ms` | Between captures. |

> **Preconditions:** a streaming-capable camera and a working trigger for the
> selected mode. `s.warmup(trigger)` runs once before measurement.

## 4. Outputs & interpretation

### Metrics

| Metric | Unit | Meaning |
| --- | --- | --- |
| `delta_mean` / `_stddev` / `_min` / `_max` / `_p95` / `_jitter` | ms | Distribution of consecutive buffer-timestamp deltas — i.e. the actual inter-frame interval as reported by the driver. |
| `wall_buf_offset_mean` / `_stddev` / `_min` / `_max` / `_p95` / `_jitter` | ms | Distribution of (application receive time − buffer timestamp) — how far the driver's timestamp sits from the moment the application actually got the frame. |
| `non_monotonic` | count | Number of frames whose buffer timestamp did not strictly increase over the previous frame. |

### Status

| Status | Condition | Meaning |
| --- | --- | --- |
| `Pass` | `non_monotonic == 0` | Every buffer timestamp strictly increased across the run. |
| `Fail` | `non_monotonic > 0` | At least one timestamp went backward or repeated — a hard timestamp-integrity violation. |
| `Error` | session open/start failed, or fewer than 2 frames captured | Not enough data to analyze (see summary). |

There is no `Warn` outcome for this test — timestamp monotonicity is treated
as a binary correctness property, unlike [t08](t08-sequence-continuity.md)
where a handful of anomalies is tolerated as `Warn`.

### How to read the numbers

- **`delta_mean` should track your trigger interval** (`100 ms` here, since
  that's the sample-loop pacing plus capture time) — a mean far from
  expectation, or a high `delta_jitter`, indicates unstable frame pacing
  independent of the monotonicity question.
- **`wall_buf_offset_mean`** is a **fixed clock-domain offset**, not a
  latency measurement — expect it to be roughly constant across the run
  (low `wall_buf_offset_stddev`/`_jitter`). A large or growing offset
  suggests clock drift between the buffer timestamp's clock source and the
  application's `CLOCK_REALTIME` reads.
- **Any `non_monotonic > 0` is a hard `Fail`**, unlike gap/duplicate counts
  elsewhere in the suite which tolerate a threshold — even one
  backward-moving timestamp means downstream ordering logic cannot trust
  this signal.
- Cross-check the timestamp **source** (SOE vs. EOF, monotonic vs. copy) via
  [t13](t13-buffer-flags.md)'s flag analysis if these numbers look
  unexpected — the meaning of "buffer timestamp" depends on which source
  flag the driver is using.

## 5. How the code works

`run_t14`:

1. **Open + start + warm-up** — opens the device, starts streaming with `2`
   buffers, and warms up. Failure yields `Error`.
2. **Capture loop** — 100 iterations, each capturing with a 100 ms timeout
   and a 100 ms pacing sleep. On success, records the buffer timestamp
   (`f.timestamp`, converted to microseconds) and the application's own
   receive timestamp (`f.t_recv`, the `CLOCK_REALTIME` mark taken right
   after `DQBUF` returns).
3. **Guard** — if fewer than 2 frames were captured, reports `Error`
   ("Insufficient frames").
4. **Analyze** — walks consecutive buffer timestamps to build the
   `deltas_ms` distribution and count `non_mono` (any timestamp ≤ the
   previous one); separately computes `offsets_ms` as
   `(wall_ts[i] - buf_ts[i])` for every captured frame.
5. **Aggregate** — pushes the `delta_*` family and `wall_buf_offset_*`
   family via `push_stats_metrics()`, plus `non_monotonic`.
6. **Verdict** — `Pass` if `non_monotonic == 0`, otherwise `Fail`.

This is the closest test in the suite to the canonical
open→warmup→capture-N→`compute_stats`→`push_stats_metrics` shape — the only
addition is computing two derived series (deltas and offsets) from the raw
timestamps before handing them to the shared stats pipeline.
