# t11 — Buffer recycling timing

> - **Implementation:** search `run_t11` in [diagnostic_runner.cpp](../../../source/backend/core/src/diagnostic_runner.cpp)
> - **Definition:** search `t11-buffer-recycling` in [test_registry.cpp](../../../source/backend/core/src/test_registry.cpp)
> - **Category:** `buffering`
> - **`uses_trigger`:** yes
> - **Trigger modes:** all (Hardware, Software, FreeRun)

## 1. Overview / scope

**What it checks:** how sensitive the pipeline is to the delay between
dequeuing a buffer (`DQBUF`) and requeuing it (`QBUF`) — does a slow consumer
cause the *next* frame to be missed?

**Questions it answers:**

- If the application takes its time processing a frame before giving the
  buffer back, at what delay does that start causing misses on the next
  capture?
- Is there a safe delay budget an application can rely on between receiving
  a frame and requeuing its buffer?

**Why it matters:** real applications don't requeue buffers instantly — they
process the frame first. This test finds the actual delay budget instead of
assuming zero-delay requeuing, which is what most other tests in this suite
do implicitly.

**Method:** sweep a delay from `0 ms` to `100 ms`; at each delay, capture a
frame, wait the delay, requeue that specific buffer, then capture again and
see whether the next frame arrives. Find the first delay where this starts
missing.

## 2. Trigger modes

No restriction — buffer recycling sensitivity is checked the same way
regardless of trigger source.

## 3. Inputs

All parameters are fixed in `run_t11`:

| Input | Value | Meaning |
| --- | --- | --- |
| Delay sweep | `0, 1, 5, 10, 20, 30, 40, 48, 50, 60, 80, 100` ms (12 values) | Dequeue-to-requeue delay tested at each step. |
| Repetitions per delay | `10` | Dequeue/requeue/dequeue cycles at each delay value. |
| Buffer count | `2` | |
| Capture timeout | `100 ms` | Per phase. |
| Inter-rep interval | `100 ms` | Between repetitions. |
| Pass threshold | cliff delay `>= 50 ms` | See Status below. |

> **Preconditions:** a streaming-capable camera and a working trigger for the
> selected mode. `s.warmup(trigger)` runs once before the sweep starts.

## 4. Outputs & interpretation

### Metrics

| Metric | Unit | Meaning |
| --- | --- | --- |
| `hits_delay_0ms` … `hits_delay_100ms` | count | Successful second-phase captures out of 10 reps, at that delay value. |
| `cliff_delay_ms` | ms | First delay value where hits dropped below 10/10. `-1` if every delay stayed at 10/10. |

`details` records one line per delay: hit count and (if any hits) mean
latency of the second-phase capture.

### Status

| Status | Condition | Meaning |
| --- | --- | --- |
| `Pass` | `cliff_delay_ms == -1` | No recycling sensitivity found up to 100 ms — the pipeline tolerates slow requeuing well. |
| `Pass` | `cliff_delay_ms >= 50` | A cliff exists, but only at 50 ms or slower — a generous delay budget for most applications. |
| `Warn` | `0 <= cliff_delay_ms < 50` | The pipeline is sensitive to requeue delay below 50 ms — a fast-processing application could still hit this. |
| `Error` | session open/start failed | Setup problem, not a result (see summary). |

### How to read the numbers

- **`cliff_delay_ms` is your safe delay budget** — if an application's
  frame-processing time before requeuing is reliably under this value, it
  should not see recycling-related misses.
- **A cliff near `48–50 ms`** roughly aligns with a single frame period at
  ~20 fps — suggests the driver needs the buffer back within about one frame
  interval to avoid a miss on the next one.
- **`hits_delay_Xms` dropping gradually** (10 → 8 → 5 → 0 as delay
  increases) suggests a soft, probabilistic sensitivity; a sharp drop
  straight to `0` at the cliff suggests a hard requirement.

## 5. How the code works

`run_t11`:

1. **Open + start + warm-up** — opens the device, starts streaming with `2`
   buffers, and warms up. Failure yields `Error`.
2. **Delay sweep** — for each of the 12 delay values, runs 10 repetitions:
   - **Phase 1 dequeue-only** — `s.capture(trigger, 100, true, false)`
     (drain enabled, requeue **disabled**) fires the trigger and dequeues a
     buffer without giving it back.
   - **Injected delay** — sleeps the current delay value (skipped at
     `0 ms`).
   - **Manual requeue** — `s.requeue(f1.index)` explicitly returns that
     specific buffer to the driver's queue.
   - **Phase 2 queue-only** — `s.capture(trigger, 100, false, true)`
     (drain **disabled**, requeue enabled) fires a second trigger and
     dequeues the resulting frame, recording its latency on success.
   - Counts hits out of 10 reps; the first delay value with `hits < 10` is
     recorded as `cliff_delay`.
3. **Aggregate** — pushes `hits_delay_<N>ms` for every delay tested, plus
   `cliff_delay_ms`.
4. **Verdict** — `Pass` if no cliff was found or the cliff is `>= 50 ms`,
   `Warn` if the cliff is below `50 ms`.

This is the only test that uses `V4lSession::capture()`'s extended
4-argument form (`do_drain`, `do_requeue`) to deliberately split dequeue and
requeue into two separately-timed phases, together with an explicit
`requeue()` call in between — every other test relies on `capture()`'s
default drain-and-requeue-together behavior.
