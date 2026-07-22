# t01 — Frame capture without STREAMON

> - **Implementation:** search `run_t01` in [diagnostic_runner.cpp](../../../source/backend/core/src/diagnostic_runner.cpp)
> - **Definition:** search `t01-no-streamon` in [test_registry.cpp](../../../source/backend/core/src/test_registry.cpp)
> - **Category:** `stream-state`
> - **`uses_trigger`:** no
> - **Trigger modes:** all (mode is irrelevant — no trigger is fired)

## 1. Overview / scope

**What it checks:** that the driver delivers **no** frames before
`VIDIOC_STREAMON` is called.

**Questions it answers:**

- Does `poll()` correctly report "no data" on a device that has buffers queued
  but streaming not started?
- Does `VIDIOC_DQBUF` correctly fail instead of returning a frame?

**Why it matters:** it is a pure V4L2 state-machine correctness check. A driver
that hands out frames before `STREAMON` breaks the fundamental capture
contract, and every other test's timing assumptions become meaningless.

**Method:** open the device, allocate buffers, but do **not** call `STREAMON`;
then probe `poll()` and `VIDIOC_DQBUF` and confirm both behave as the V4L2
spec requires.

## 2. Trigger modes

No trigger is fired, so the trigger mode does not affect this test — it runs
identically under Hardware, Software, and free-run.

## 3. Inputs

All parameters are currently fixed in `run_t01`:

| Input | Value | Meaning |
| --- | --- | --- |
| Buffer count | `2` | Buffers requested via `VIDIOC_REQBUFS` (no streaming started). |
| Poll timeout | `50 ms` | Wait passed to `poll()` while streaming is off. |
| Memory backend | selected run backend | `mmap` / `dmabuf` / `userptr`. |

> **Preconditions:** a V4L2 capture device that can allocate buffers. No
> trigger, GPIO, or streaming camera motion is required.

## 4. Outputs & interpretation

### Metrics

| Metric | Unit | Meaning |
| --- | --- | --- |
| `poll_returned` | count | `poll(50ms)` return value with streaming off. Expected `0`. |
| `dqbuf_failed` | bool | `1` if `VIDIOC_DQBUF` correctly failed, `0` if it returned a frame. Expected `1`. |
| `dqbuf_errno` | errno | `errno` from `DQBUF`. Expected `EAGAIN` (11) or `EINVAL` (22). |

`details` records the human-readable poll and DQBUF outcomes.

### Status

| Status | Condition | Meaning |
| --- | --- | --- |
| `Pass` | `poll` returned `0` **and** `DQBUF` failed with `EAGAIN`/`EINVAL` | Correct — no frames before `STREAMON`. |
| `Fail` | `DQBUF` returned a frame | State-machine violation — the driver delivered data without `STREAMON`. |
| `Warn` | `DQBUF` failed correctly, but `poll` returned non-zero | DQBUF behaves, but `poll` signaled readiness it should not have. |
| `Error` | device open or `REQBUFS` failed | Setup problem, not a result (see summary). |

### How to read it

This is a **correctness** test, not a measurement — the verdict is
categorical, there are no thresholds to tune.

- `Pass` is the only healthy outcome.
- `Fail` is a real driver bug: frames must never appear before `STREAMON`.
- `Warn` is usually benign but worth noting — some drivers wake `poll()` on a
  non-data condition; DQBUF still gates the frame correctly.

## 5. How the code works

`run_t01`:

1. **Open + allocate** — opens the device and calls `setup_buffers(2, backend)`
   to request buffers, but deliberately never calls `STREAMON`. A failure here
   yields `Error`.
2. **Probe poll** — runs `poll()` with a 50 ms timeout and records the return
   value (`poll_returned`).
3. **Probe DQBUF** — issues `VIDIOC_DQBUF` once and captures both the return
   value and `errno` (`dqbuf_failed`, `dqbuf_errno`).
4. **Verdict** — `Pass` when poll returned 0 and DQBUF failed with
   `EAGAIN`/`EINVAL`; `Fail` when a frame came back; `Warn` when DQBUF failed
   correctly but poll returned non-zero.
