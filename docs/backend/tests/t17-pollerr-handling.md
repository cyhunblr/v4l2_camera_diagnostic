# t17 — POLLERR/POLLHUP handling

> - **Implementation:** search `run_t17` in [diagnostic_runner.cpp](../../../source/backend/core/src/diagnostic_runner.cpp)
> - **Definition:** search `t17-pollerr-handling` in [test_registry.cpp](../../../source/backend/core/src/test_registry.cpp)
> - **Category:** `stream-state`
> - **`uses_trigger`:** yes
> - **Trigger modes:** all (Hardware, Software, FreeRun)
> - **Experimental, risky:** yes — deliberately stops streaming mid-run and probes error-path behavior

## 1. Overview / scope

**What it checks:** whether the driver correctly rejects `DQBUF` after
`STREAMOFF`, whether `poll()` raises `POLLERR`/`POLLHUP` in that state, and
whether the stream can be cleanly recovered by calling `STREAMON` again.

**Questions it answers:**

- After stopping the stream, does the fd still (incorrectly) deliver frames?
- Does `poll()` signal an error condition (`POLLERR`/`POLLHUP`) once
  streaming has stopped, or does it stay silent?
- Can the stream be restarted and resume normal capture afterward?

**Why it matters:** every other test either streams continuously or cycles
STREAMON/STREAMOFF cleanly between full session teardowns
([t12](t12-stream-cycles.md)). This test specifically probes the **error
path** — stopping mid-stream and immediately trying to read from it — which
is a state applications can hit accidentally (e.g. a race between stopping
and a pending read).

**Method:** capture a small baseline, call `STREAMOFF`, immediately fire
another trigger and attempt to `poll()`/`DQBUF` against the now-stopped fd,
then call `STREAMON` again and verify normal capture resumes.

## 2. Trigger modes

No restriction — the STREAMOFF/recovery behavior is checked the same way
regardless of trigger source.

## 3. Inputs

All parameters are fixed in `run_t17`:

| Input | Value | Meaning |
| --- | --- | --- |
| Baseline captures | `3` | Before `STREAMOFF`, to confirm the stream works normally first. |
| Warm-up (baseline + recovery) | `3` triggers @ `200 ms` | Run before each capture phase. |
| Post-STREAMOFF poll timeout | `100 ms` | Wait for `POLLERR`/`POLLHUP` after stopping. |
| Recovery captures | `3` | After re-`STREAMON`, to confirm normal capture resumed. |
| Buffer count | `2` | |
| Capture timeout | `100 ms` | Per sample, baseline and recovery. |

> **Preconditions:** a streaming-capable camera and a working trigger for
> the selected mode.

## 4. Outputs & interpretation

### Metrics

| Metric | Unit | Meaning |
| --- | --- | --- |
| `baseline_ok` | count | Successful captures out of 3, before `STREAMOFF`. |
| `pollerr_raised` | bool | Whether `poll()` reported `POLLERR` after `STREAMOFF`. |
| `pollhup_raised` | bool | Whether `poll()` reported `POLLHUP` after `STREAMOFF`. |
| `dqbuf_failed` | bool | Whether `VIDIOC_DQBUF` correctly failed after `STREAMOFF`. |
| `restreamon_ok` | bool | Whether re-issuing `VIDIOC_STREAMON` succeeded. |
| `recovery_ok` | count | Successful captures out of 3, after re-`STREAMON`. |

`details` records the raw `poll()` return value with any raised flags, and
the `DQBUF` outcome with its `errno`.

### Status

| Status | Condition | Meaning |
| --- | --- | --- |
| `Pass` | `DQBUF` failed **and** re-`STREAMON` succeeded **and** `recovery_ok >= 2` | Correct error behavior, and the stream recovers reliably afterward. |
| `Fail` | `DQBUF` **succeeded** after `STREAMOFF` | A real state-machine violation — the driver delivered a frame after being told to stop. |
| `Warn` | `DQBUF` failed correctly, but `recovery_ok < 2` | The error path itself is correct, but restarting the stream afterward is not fully reliable. |

### How to read the numbers

- **`dqbuf_failed == true` is the critical correctness signal** — this
  mirrors [t01](t01-no-streamon.md)'s "no frames before STREAMON" check, but
  for the opposite transition ("no frames after STREAMOFF").
  `pollerr_raised`/`pollhup_raised` are secondary — some valid driver
  implementations may not raise either flag even though `DQBUF` correctly
  fails, so their absence alone is not necessarily a problem.
- **`Fail` here means the same thing as a `Fail` in
  [t01](t01-no-streamon.md)**: the driver is handing out frames when it
  should not be, just triggered from the other end of the stream lifecycle.
- **`recovery_ok < 2` with a correct `dqbuf_failed`** — the error path is
  fine, but something about resuming capture after the STREAMOFF/STREAMON
  cycle is flaky. Compare against
  [t12](t12-stream-cycles.md)'s full-cycle-from-scratch reliability numbers
  — this test's recovery is a *warm* restart (same session, same buffers),
  which should generally be at least as reliable.

## 5. How the code works

`run_t17`:

1. **Open + start + warm-up** — opens the device, starts streaming with `2`
   buffers, and warms up with 3 triggers. Failure yields `Error`.
2. **Baseline** — captures 3 samples to confirm normal operation before
   disturbing the stream.
3. **Stop the stream** — calls `s.streamoff()`.
4. **Probe the stopped state** — fires **another** trigger (even though
   streaming is off), then `poll()`s the fd with a 100 ms timeout, recording
   whether `POLLERR`/`POLLHUP` were raised. Immediately follows with a raw
   `VIDIOC_DQBUF` attempt, recording whether it failed and its `errno`.
5. **Recover** — calls `s.streamon()` again; if that succeeds, warms up
   again and captures 3 more samples to confirm the stream resumed
   correctly.
6. **Verdict** — `Fail` if `DQBUF` unexpectedly succeeded after
   `STREAMOFF`; `Pass` if it correctly failed, re-`STREAMON` succeeded, and
   at least 2/3 recovery captures worked; otherwise `Warn`.

This is one of the few tests in the suite that deliberately puts the stream
into an **error state** on purpose, rather than measuring normal operation —
hence its `risky`/`experimental` classification in the registry.
