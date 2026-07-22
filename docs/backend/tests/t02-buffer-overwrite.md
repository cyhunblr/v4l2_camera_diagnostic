# t02 — Buffer overwrite behavior

> - **Implementation:** search `run_t02` in [diagnostic_runner.cpp](../../../source/backend/core/src/diagnostic_runner.cpp)
> - **Definition:** search `t02-buffer-overwrite` in [test_registry.cpp](../../../source/backend/core/src/test_registry.cpp)
> - **Category:** `buffering`
> - **`uses_trigger`:** yes
> - **Trigger modes:** Hardware, Software (no free-run)
> - **Risky:** yes — deliberately floods the driver's buffer queue

## 1. Overview / scope

**What it checks:** what happens when far more triggers fire than there are
buffers to hold the resulting frames, with `VIDIOC_DQBUF` never called during
the flood.

**Questions it answers:**

- Does the driver silently overwrite in-flight buffers, or does it correctly
  reject/queue excess frames?
- Do frames that are eventually dequeued carry `V4L2_BUF_FLAG_ERROR`?

**Why it matters:** every triggered pipeline risks a burst of triggers
outrunning the consumer. This test characterizes that failure mode directly
instead of assuming it away.

**Method:** with only 2 buffers allocated, fire a large burst of untouched
triggers (no `DQBUF` in between), then drain whatever the driver produced and
count how many frames are available and how many are flagged as errors. Two
burst variants (different trigger counts/intervals) are run back-to-back.

## 2. Trigger modes

Mask `0x01 | 0x02` — **Hardware** and **Software** only.

- **Hardware** — GPIO pulse per trigger in the burst.
- **Software** — V4L2 control write per trigger in the burst.
- **FreeRun** — not supported; reported `Skipped`. Overwrite behavior is
  meaningless without a trigger driving the burst.

## 3. Inputs

All parameters are fixed in `run_t02`:

| Input | Value | Meaning |
| --- | --- | --- |
| Buffer count | `2` | Deliberately small — this is the queue being overrun. |
| Variant A | `100` triggers @ `100 ms` interval | First burst. |
| Variant B | `200` triggers @ `50 ms` interval | Second, faster burst. |
| Pre-burst settle | `500 ms` + drain | Lets the stream reach steady state before flooding it. |
| Trigger send interval | `interval_ms - 13` | 13 ms is subtracted to account for the fixed trigger pulse width. |

> **Preconditions:** a streaming-capable camera plus a working trigger for the
> selected mode. Each variant opens a fresh session.

## 4. Outputs & interpretation

### Metrics

| Metric | Unit | Meaning |
| --- | --- | --- |
| `triggers_A` | count | Triggers sent in Variant A (`100`). |
| `frames_available_A` | count | Frames drained from the queue after Variant A's burst. |
| `triggers_B` | count | Triggers sent in Variant B (`200`). |
| `frames_available_B` | count | Frames drained from the queue after Variant B's burst. |
| `error_flag_total` | count | Frames across **both** variants carrying `V4L2_BUF_FLAG_ERROR`. |

`details` records one line per variant: buffer count, trigger count,
frames available, and error count (if any).

### Status

| Status | Condition | Meaning |
| --- | --- | --- |
| `Pass` | `error_flag_total == 0` | No frame in either burst was flagged as an error. |
| `Warn` | `error_flag_total > 0` | At least one drained frame carried `V4L2_BUF_FLAG_ERROR`. |
| `Error` | session open/start failed | Setup problem, not a result (see summary). |

There is no `Fail` outcome — this test characterizes overwrite behavior, it
does not assert a specific "correct" frame count (with only 2 buffers, most
triggers in a 100- or 200-trigger burst are expected to have nowhere to land).

### How to read the numbers

- **`frames_available_A/B`** — with 2 buffers, expect this to be small
  (on the order of the buffer count), not close to the trigger count. A value
  much closer to the trigger count would suggest buffers are being drained
  by something other than this test's flood loop.
- **`error_flag_total`** — the interesting signal. `0` means the driver is
  quietly dropping/ignoring excess triggers without corrupting queued
  buffers. Any nonzero value means the driver handed back at least one frame
  it flagged as bad — worth comparing against Variant A vs. B to see whether
  the faster burst (B) makes it worse.

## 5. How the code works

`run_t02` runs each `Variant` (A then B) through the same sequence:

1. **Open + start** — opens the device and starts streaming with `2` buffers
   via `V4lSession`. Failure yields `Error` and aborts the whole test (not
   just the current variant).
2. **Settle** — sleeps 500 ms then calls `drain()` to flush any frames from
   session start-up before the flood begins.
3. **Flood** — sends `v.triggers` triggers back-to-back via
   `trigger.send()`, sleeping `interval_ms - 13` between each. Crucially,
   **no `DQBUF` is issued during this loop** — the driver's queue is left to
   fill up or overwrite on its own.
4. **Drain and count** — after the flood, issues raw `VIDIOC_DQBUF` calls in
   a loop until the driver returns an error (queue empty), counting
   `available` frames and how many carry `V4L2_BUF_FLAG_ERROR`.
5. **Record** — pushes the per-variant metrics and details, accumulating
   `error_frames_total` across both variants.

After both variants run, the verdict is `Warn` if any error-flagged frame was
seen, otherwise `Pass`. Unlike the canonical capture tests, `run_t02` never
calls `V4lSession::capture()` or `warmup()` — it uses raw ioctl calls so the
buffer queue is left completely untouched during the flood.
