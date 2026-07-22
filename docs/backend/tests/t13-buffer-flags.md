# t13 — V4L2 buffer flag analysis

> - **Implementation:** search `run_t13` in [diagnostic_runner.cpp](../../../source/backend/core/src/diagnostic_runner.cpp)
> - **Definition:** search `t13-buffer-flags` in [test_registry.cpp](../../../source/backend/core/src/test_registry.cpp)
> - **Category:** `metadata`
> - **`uses_trigger`:** yes
> - **Trigger modes:** all (Hardware, Software, FreeRun)

## 1. Overview / scope

**What it checks:** which `V4L2_BUF_FLAG_*` bits the driver actually sets on
returned buffers, across a run of captures, and whether the timestamp-source
flag is used **consistently** from frame to frame.

**Questions it answers:**

- Are any frames flagged with `V4L2_BUF_FLAG_ERROR`?
- Does the driver mark frames as keyframes, and does that make sense for
  this sensor/format?
- Does the driver consistently report the same timestamp source (monotonic
  vs. copy, start-of-exposure vs. end-of-frame) across every frame, or does
  it switch?

**Why it matters:** buffer flags are metadata other tests and applications
rely on implicitly (e.g. [t14](t14-timestamp-monotonicity.md) trusts the
buffer timestamp; [t08](t08-sequence-continuity.md) trusts monotonic
ordering). This test surfaces what the driver is actually reporting instead
of assuming a specific flag convention.

**Method:** capture 50 triggered frames, OR all their flag bitmasks together
for a combined view, and tally how many frames set each flag of interest
individually.

## 2. Trigger modes

No restriction — flag reporting is checked the same way regardless of
trigger source.

## 3. Inputs

All parameters are fixed in `run_t13`:

| Input | Value | Meaning |
| --- | --- | --- |
| Sample count | `50` | Triggered frames captured. |
| Buffer count | `2` | |
| Capture timeout | `100 ms` | Per sample. |
| Sample interval | `100 ms` | Between captures. |

> **Preconditions:** a streaming-capable camera and a working trigger for the
> selected mode. `s.warmup(trigger)` runs once before measurement.

## 4. Outputs & interpretation

### Metrics

| Metric | Unit | Meaning |
| --- | --- | --- |
| `frames_captured` | count | Frames successfully captured out of 50 attempts. |
| `flag_error` | count | Frames with `V4L2_BUF_FLAG_ERROR` set. |
| `flag_keyframe` | count | Frames with `V4L2_BUF_FLAG_KEYFRAME` set. |
| `flag_ts_monotonic` | count | Frames with `V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC` set. |
| `flag_ts_copy` | count | Frames with `V4L2_BUF_FLAG_TIMESTAMP_COPY` set. |
| `flag_soe` | count | Frames with `V4L2_BUF_FLAG_TSTAMP_SRC_SOE` (start-of-exposure) set. |
| `flag_eof` | count | Frames **without** the SOE bit — i.e. using end-of-frame timestamp source (SOE and EOF are mutually exclusive by construction in this test). |

`details` records the bitwise-OR of every frame's flags as a hex value
(showing every flag bit seen across the whole run at a glance), and whether
the timestamp source was used consistently.

### Status

| Status | Condition | Meaning |
| --- | --- | --- |
| `Pass` | `flag_error == 0` | No frame was flagged as an error by the driver. |
| `Warn` | `flag_error > 0` | At least one frame carried `V4L2_BUF_FLAG_ERROR` — worth correlating with capture-quality issues seen in other tests. |
| `Error` | session open/start failed | Setup problem, not a result (see summary). |

### How to read the numbers

- **`flag_ts_monotonic` and `flag_ts_copy` should each be either `0` or equal
  to `frames_captured`** — a driver should pick one timestamp semantics and
  stick with it. The "Timestamp source consistent" detail line checks
  exactly this (both counts are either all-frames or zero on each field).
  If it says "no", the driver is switching timestamp semantics mid-stream,
  which would break any test or application computing timestamp deltas.
- **`flag_soe` vs `flag_eof`** — tells you which point in the exposure the
  reported timestamp corresponds to. This matters for latency
  interpretation: an SOE-sourced timestamp measures from the start of
  exposure, while EOF measures from the end, so the same physical event can
  look like a different photo of latency depending on which source the
  driver uses. Cross-check against [t03](t03-gpio-latency.md)'s latency
  numbers if the source is unexpected.
- **`flag_keyframe`** — mostly relevant for compressed formats; expect `0`
  for raw/uncompressed captures.

## 5. How the code works

`run_t13`:

1. **Open + start + warm-up** — opens the device, starts streaming with `2`
   buffers, and warms up. Failure yields `Error`.
2. **Capture loop** — 50 iterations, each capturing with a 100 ms timeout
   and a 100 ms pacing sleep. On success, ORs the frame's `flags` into a
   running combined mask
   and increments the specific counters for `ERROR`, `KEYFRAME`,
   `TIMESTAMP_MONOTONIC`, `TIMESTAMP_COPY`, and `TSTAMP_SRC_SOE` (with
   everything not-SOE counted as EOF).
3. **Aggregate** — pushes all seven count metrics, plus the combined
   flags-OR hex string and the timestamp-source-consistency check into
   `details`.
4. **Verdict** — `Warn` if any frame had `V4L2_BUF_FLAG_ERROR`, otherwise
   `Pass`.

This is a purely bitwise-counting test — unlike most other tests in this
suite, `run_t13` never calls `compute_stats()`; its output is entirely
per-flag frequency counts derived from `V4l2Buffer::flags`.
