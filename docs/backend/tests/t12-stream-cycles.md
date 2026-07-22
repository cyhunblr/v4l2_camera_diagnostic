# t12 — STREAMON/STREAMOFF cycle reliability

> - **Implementation:** search `run_t12` in [diagnostic_runner.cpp](../../../source/backend/core/src/diagnostic_runner.cpp)
> - **Definition:** search `t12-stream-cycles` in [test_registry.cpp](../../../source/backend/core/src/test_registry.cpp)
> - **Category:** `stream-state`
> - **`uses_trigger`:** yes
> - **Trigger modes:** all (Hardware, Software, FreeRun)

## 1. Overview / scope

**What it checks:** whether the device reliably supports being started and
stopped repeatedly — both a normal "warm up, capture a few, done" cycle and a
rapid open/start/capture-once/close cycle back-to-back.

**Questions it answers:**

- Does a full open→stream→warm-up→capture→teardown cycle succeed reliably
  across 20 repetitions?
- Does a much faster, minimal-warm-up cycle (open→stream→one
  capture→teardown, 50 times, 10 ms apart) still deliver frames most of the
  time?

**Why it matters:** every other test in this suite opens **one** session and
runs its measurement inside it. This test instead exercises the
open/stream/close lifecycle itself repeatedly — the thing an application
does every time it starts or restarts a capture session.

**Method:** run 20 "full" cycles (fresh session, warm-up, 5 captures) and 50
"rapid" cycles (fresh session, one capture, minimal pacing), each with its
own session opened and torn down from scratch, and tally success rates for
both.

## 2. Trigger modes

No restriction — cycle reliability is checked the same way regardless of
trigger source.

## 3. Inputs

All parameters are fixed in `run_t12`:

| Input | Value | Meaning |
| --- | --- | --- |
| Full cycles | `20` | Each: fresh session, warm-up, 5 captures, then discarded. |
| Full-cycle warm-up | `3` triggers @ `200 ms` | Per full cycle, before its 5 captures. |
| Full-cycle captures | `5` per cycle | 100 ms timeout each, 100 ms apart; first capture's latency is recorded separately. |
| Rapid cycles | `50` | Each: fresh session, **one** capture, then discarded. |
| Rapid-cycle capture | `1` per cycle | 200 ms timeout, no warm-up. |
| Rapid-cycle pacing | `10 ms` | Between rapid cycles. |
| Buffer count | `2` | Every session, full or rapid. |

> **Preconditions:** a streaming-capable camera and a working trigger for the
> selected mode. This test opens **70 sessions total** (20 + 50) over its
> run.

## 4. Outputs & interpretation

### Metrics

| Metric | Unit | Meaning |
| --- | --- | --- |
| `full_cycles_success` | count | Full cycles where the session opened, started, **and** all 5 captures succeeded. |
| `full_cycle_failures` | count | Full cycles where session setup failed **or** fewer than 5/5 captures succeeded. |
| `rapid_cycles_ok` | count | Rapid cycles that delivered a frame. |
| `rapid_cycles_total` | count | Always `50`. |
| `first_frame_latency_mean` / `_stddev` / `_min` / `_max` / `_p95` / `_jitter` | ms | Latency of each full cycle's **first** capture only (present if ≥1 full cycle captured a first frame). |

### Status

| Status | Condition | Meaning |
| --- | --- | --- |
| `Pass` | `full_cycle_failures == 0` **and** rapid success rate `>= 90%` | Both cycle types are fully reliable. |
| `Warn` | `full_cycle_failures <= 2` **and** rapid success rate `>= 70%` | Mostly reliable, with a small number of full-cycle failures or a lower rapid success rate. |
| `Fail` | neither condition met | Repeated stream lifecycle failures — worth investigating before relying on frequent session restarts. |

There is no `Error` status for this test — a session setup failure during
either loop counts as a cycle failure (or is silently skipped for rapid
cycles) rather than aborting the whole test, since the point is to measure
how often the lifecycle succeeds, not to require every attempt to work.

### How to read the numbers

- **`full_cycle_failures` vs `rapid_cycles_ok`** — full cycles include a
  warm-up and 5 captures, so they are a stricter test of sustained
  reliability per session; rapid cycles test raw open/stream/capture-once
  speed under tight repetition. A system that passes rapid but fails full
  cycles may have an issue that only appears after a few captures per
  session (rare, but distinguishable from a pure open/close problem).
- **`first_frame_latency_*`** — this is specifically the latency of the
  *first* capture in each full cycle, i.e. right after warm-up. Compare
  against [t03](t03-gpio-latency.md)'s steady-state latency (measured in a
  single long-lived session) to see whether the first frame after a fresh
  session start costs more than steady-state captures do.
- **Rapid success rate below 90%** with **zero** full-cycle failures
  suggests the issue is specifically about speed of restart (10 ms pacing,
  no warm-up), not general session reliability.

## 5. How the code works

`run_t12`:

1. **Full-cycle loop** (`20` iterations) — each iteration opens a **brand
   new** `V4lSession`, starts streaming with `2` buffers; a setup failure
   increments `full_failures` and moves to the next iteration (`continue`).
   On success, warms up with 3 triggers, then captures 5 times (100 ms
   timeout, 100 ms apart), recording the first capture's latency separately
   and counting successes (`ok`). If fewer than all 5 captures succeeded,
   the cycle counts as a failure too.
2. **Rapid-cycle loop** (`50` iterations) — each opens a fresh session,
   starts streaming, and attempts exactly **one** capture (200 ms timeout,
   no warm-up); a setup failure is silently skipped (`continue`, not counted
   as a rapid failure explicitly — it simply does not increment
   `rapid_ok`). Sessions are 10 ms apart.
3. **Aggregate** — pushes the four cycle-count metrics, plus
   `first_frame_latency_*` if any full cycle captured a first frame.
4. **Verdict** — thresholds on `full_cycle_failures` and the rapid success
   percentage as described above.

This is the only test in the suite that opens a fresh `V4lSession` inside
its measurement loop — every other test opens one session up front and
performs all its measurement within that single stream lifetime.
