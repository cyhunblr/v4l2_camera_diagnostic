# t21 — Stuck frame detection

> - **Implementation:** search `run_t21` in [diagnostic_runner.cpp](../../../source/backend/core/src/diagnostic_runner.cpp)
> - **Definition:** search `t21-stuck-frame` in [test_registry.cpp](../../../source/backend/core/src/test_registry.cpp)
> - **Category:** `quality`
> - **`uses_trigger`:** yes
> - **Trigger modes:** all (Hardware, Software, FreeRun)
> - Not in the original legacy diagnostic — added when the modular runner was built.

## 1. Overview / scope

**What it checks:** whether the sensor ever delivers the **same image data**
for consecutive triggered frames — a frozen or stuck camera output — by
directly comparing frame content, not just metadata like sequence numbers.

**Questions it answers:**

- Does any pair of consecutive frames contain byte-for-byte identical image
  data?
- If identical frames occur, are they isolated one-off repeats, or a long
  consecutive run (i.e. the camera actually froze for a while)?

**Why it matters:** [t08](t08-sequence-continuity.md) checks that
`sequence` numbers advance correctly, but a stuck sensor could still
increment `sequence` while returning the same underlying pixel data. This
test is the suite's only check on actual frame **content**, catching a
failure mode that pure metadata checks cannot see.

**Method:** capture 50 triggered frames; for each, compare the first 4 KB of
its buffer against the previous frame's first 4 KB; count identical pairs
and track the longest run of consecutive identical frames.

## 2. Trigger modes

No restriction — stuck-frame detection is checked the same way regardless
of trigger source.

## 3. Inputs

All parameters are fixed in `run_t21`:

| Input | Value | Meaning |
| --- | --- | --- |
| Sample count | `50` | Triggered frames captured. |
| Bytes compared | `4096` | Leading bytes of each frame's buffer compared against the previous frame. |
| Buffer count | `2` | |
| Capture timeout | `100 ms` | Per sample. |
| Sample interval | `100 ms` | Between captures. |
| Fail threshold | `max_identical_run >= 5` | See Status below. |

> **Preconditions:** a streaming-capable camera and a working trigger for
> the selected mode. `s.warmup(trigger)` runs once before measurement.

## 4. Outputs & interpretation

### Metrics

| Metric | Unit | Meaning |
| --- | --- | --- |
| `frames_tested` | count | Frames with enough data (≥4096 bytes) to be compared. |
| `identical_pairs` | count | Consecutive frame pairs found byte-identical in their first 4 KB. |
| `max_identical_run` | count | Longest unbroken run of consecutive identical frames observed. |

### Status

| Status | Condition | Meaning |
| --- | --- | --- |
| `Pass` | `identical_pairs == 0` | Every tested frame differed from its predecessor — no stuck output detected. |
| `Fail` | `max_identical_run >= 5` | 5 or more consecutive frames were byte-identical — the sensor appears to have frozen for a meaningful stretch. |
| `Warn` | `identical_pairs > 0` but `max_identical_run < 5` | Some identical pairs occurred, but never in a long run — likely isolated repeats rather than a genuine freeze. |
| `Error` | session open/start failed, or fewer than 2 frames were comparable | Not enough data to analyze (see summary). |

### How to read the numbers

- **`max_identical_run` is the key signal, not `identical_pairs` alone** —
  a handful of scattered identical pairs (`Warn`) is a different problem
  than one long identical run (`Fail`): the former could be a genuinely
  static scene or a sensor quirk, the latter looks like the camera actually
  stopped updating.
- **A completely static scene can produce false-positive identical
  frames** even with a healthy camera — if `Warn`/`Fail` appears
  unexpectedly, consider whether the scene itself was static during the
  test run before concluding the camera is at fault.
- **Only the first 4 KB is compared**, not the full frame — this is a fast
  content probe, not an exhaustive check. A sensor that freezes only in
  regions beyond the first 4 KB would not be caught by this test.

## 5. How the code works

`run_t21`:

1. **Open + start + warm-up** — opens the device, starts streaming with `2`
   buffers, and warms up. Failure yields `Error`.
2. **Capture + compare loop** — 50 iterations, each capturing with
   drain-only (no auto-requeue, so the buffer is still valid to read).
   Frames with too little data are requeued and skipped. For valid frames,
   copies the first 4096 bytes, requeues the buffer, then `memcmp`s the
   copy against the **previous** frame's saved 4096 bytes (skipped on the
   very first tested frame, since there is no predecessor yet). A match
   increments `identical` and the current consecutive-run counter
   (updating `max_run`); a mismatch resets the run counter to zero.
3. **Aggregate** — pushes `frames_tested`, `identical_pairs`,
   `max_identical_run`.
4. **Verdict** — `Error` if fewer than 2 frames were tested; `Pass` if zero
   identical pairs occurred; `Fail` if the longest run reached 5 or more;
   otherwise `Warn`.

This test is one of three added when the modular runner was built that never
existed in the original legacy diagnostic (alongside
[t22](t22-latency-under-load.md) and [t24](t24-control-inventory.md)) — see
[docs/testing.md](../../testing.md) for the full migration inventory.
