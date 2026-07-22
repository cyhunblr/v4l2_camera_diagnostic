# t08 — Sequence number continuity

> - **Implementation:** search `run_t08` in [diagnostic_runner.cpp](../../../source/backend/core/src/diagnostic_runner.cpp)
> - **Definition:** search `t08-sequence-continuity` in [test_registry.cpp](../../../source/backend/core/src/test_registry.cpp)
> - **Category:** `sequence`
> - **`uses_trigger`:** yes
> - **Trigger modes:** all (Hardware, Software, FreeRun)

## 1. Overview / scope

**What it checks:** whether the V4L2 buffer `sequence` numbers and buffer
timestamps returned by the driver are internally consistent across a run —
no unexplained gaps, no duplicates, no timestamps going backward.

**Questions it answers:**

- Are any frames silently dropped between captures (a gap in `sequence`)?
- Does the driver ever hand back the same frame twice (duplicate
  `sequence`)?
- Do buffer timestamps ever go **backward** relative to the previous frame?

**Why it matters:** `sequence` and buffer timestamps are the ground truth an
application uses to detect drops and reorder frames. If they are not
reliable, every downstream frame-accounting assumption is unsafe.

**Method:** capture 100 triggered frames, record each one's `sequence` number
and buffer timestamp, then walk the list looking for gaps, duplicates, and
timestamp regressions.

## 2. Trigger modes

No restriction — sequence/timestamp integrity is checked the same way
regardless of trigger source.

## 3. Inputs

All parameters are fixed in `run_t08`:

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
| `frames_captured` | count | Frames successfully captured out of 100 attempts. |
| `dropped_frames` | count | Total sequence-gap size summed across the run (e.g. a jump of 3 counts as 2 dropped). |
| `max_gap` | count | The single largest sequence gap observed. |
| `duplicates` | count | Times consecutive captures returned the **same** `sequence` number. |
| `ts_non_monotonic` | count | Times a buffer timestamp was ≤ the previous frame's timestamp. |

`details` records the observed sequence range (`first → last`).

### Status

| Status | Condition | Meaning |
| --- | --- | --- |
| `Pass` | zero gaps, duplicates, **and** non-monotonic timestamps | Every captured frame is accounted for and correctly ordered. |
| `Warn` | some anomaly present, but `dropped_frames <= 5` **and** `ts_non_monotonic == 0` | Minor, bounded sequence gaps with no timestamp integrity issue. |
| `Fail` | `dropped_frames > 5` **or** `ts_non_monotonic > 0` | Either a large number of dropped frames, or **any** timestamp went backward — the latter is treated as serious regardless of how small. |
| `Error` | session open/start failed, or fewer than 2 frames captured | Not enough data to analyze (see summary). |

### How to read the numbers

- **`ts_non_monotonic > 0` is always significant** — even a single
  backward-moving timestamp forces `Fail`, unlike gaps which tolerate up to
  5 before failing. A monotonic timestamp source is a harder guarantee than
  "no big gaps", so the threshold reflects that.
- **`dropped_frames` vs `max_gap`** — many small gaps (`dropped_frames` high,
  `max_gap` low) suggests a systemic pacing issue; one big gap
  (`max_gap` close to `dropped_frames`) suggests a single stall event.
- **`duplicates`** — does not by itself trigger `Fail`; it factors into the
  Warn/Pass distinction. Any nonzero value is still worth investigating, as
  duplicate sequence numbers usually mean a requeue or buffer-reuse issue.
- Compare against [t14](t14-timestamp-monotonicity.md), which performs a
  deeper statistical analysis of timestamp deltas over 100 frames — this
  test only counts backward-moving timestamps as a binary anomaly.

## 5. How the code works

`run_t08`:

1. **Open + start + warm-up** — opens the device, starts streaming with `2`
   buffers, and warms up. Failure yields `Error`.
2. **Capture loop** — 100 iterations, each capturing with a 100 ms timeout
   and a 100 ms pacing sleep; on success, records the frame's `sequence` and
   buffer timestamp (converted to microseconds).
3. **Guard** — if fewer than 2 frames were captured, there is nothing to
   compare and the test reports `Error`.
4. **Analyze** — walks consecutive pairs of captured frames: a `sequence`
   gap of exactly `0` counts as a duplicate; a gap `> 1` adds `(gap - 1)` to
   `dropped_frames` and updates `max_gap`; any timestamp that did not
   strictly increase counts toward `ts_non_monotonic`.
5. **Verdict** — `Pass` if all three anomaly counters are zero; otherwise
   `Fail` if `dropped_frames > 5` or `ts_non_monotonic > 0`, else `Warn`.

Unlike the latency-focused tests, `run_t08` never calls `compute_stats()` or
`push_stats_metrics()` — its analysis is purely integer bookkeeping over
`sequence` and timestamp fields.
