# t04 — NON_BLOCK vs BLOCK comparison

> - **Implementation:** search `run_t04` in [diagnostic_runner.cpp](../../../source/backend/core/src/diagnostic_runner.cpp)
> - **Definition:** search `t04-nonblock-vs-block` in [test_registry.cpp](../../../source/backend/core/src/test_registry.cpp)
> - **Category:** `io-mode`
> - **`uses_trigger`:** yes
> - **Trigger modes:** all (Hardware, Software, FreeRun)

## 1. Overview / scope

**What it compares:** capture latency and CPU cost of two I/O strategies —
spin-polling a **non-blocking** fd vs. using `poll()` on a **blocking** fd.

**Questions it answers:**

- Does non-blocking spin-polling capture frames with lower or higher latency
  than blocking `poll()`?
- How many wasted `EAGAIN` iterations does the non-blocking approach burn per
  frame (a proxy for CPU cost)?

**Why it matters:** applications choose between these two I/O models when
integrating V4L2 capture. This test quantifies the trade-off on the actual
hardware instead of relying on general assumptions about non-blocking I/O.

**Method:** run two independent sessions, one per I/O mode, each firing 30
triggers and measuring trigger→frame latency with its own strategy; report
both distributions side by side.

## 2. Trigger modes

No restriction — this test measures I/O strategy, not the trigger path, so it
runs the same way under Hardware, Software, and FreeRun.

## 3. Inputs

All parameters are fixed in `run_t04`:

| Input | Value | Meaning |
| --- | --- | --- |
| Sample count | `30` (each mode) | Triggered frames measured per I/O strategy. |
| Buffer count | `2` | Both sessions. |
| NON_BLOCK spin deadline | `100 ms` | Max time spent spin-checking `EAGAIN` per sample. |
| NON_BLOCK pre-trigger delay | `10 ms` | After draining, before firing the trigger. |
| BLOCK warm-up | `5` triggers | Discarded, 200 ms `poll()` timeout each. |
| BLOCK poll timeout | `200 ms` | Per measured sample. |
| Sample interval | `200 ms` | Both modes, between samples. |

> **Preconditions:** a streaming-capable camera and a working trigger for the
> selected mode.

## 4. Outputs & interpretation

### Metrics

| Metric | Unit | Meaning |
| --- | --- | --- |
| `nonblock_latency_mean` / `_stddev` / `_min` / `_max` / `_p95` / `_jitter` | ms | Latency distribution for the NON_BLOCK strategy. Only present if ≥1 frame was captured. |
| `avg_eagain_spins` | count | Average number of `EAGAIN` spin iterations per captured frame (NON_BLOCK only). |
| `nonblock_captures` | count | Frames captured with NON_BLOCK. |
| `block_latency_mean` / `_stddev` / `_min` / `_max` / `_p95` / `_jitter` | ms | Latency distribution for the BLOCK strategy. Only present if ≥1 frame was captured. |
| `block_captures` | count | Frames captured with BLOCK. |

### Status

| Status | Condition | Meaning |
| --- | --- | --- |
| `Pass` | ≥1 frame captured in **either** mode | Comparison produced usable data; see summary for per-mode counts. |
| `Fail` | 0 frames in **both** modes | Neither strategy captured anything — likely a trigger or capture-path problem, not an I/O-mode difference. |
| `Error` | either session's open/start failed | Setup problem (see summary for which mode). |

### How to read the numbers

- **Latency: NON_BLOCK vs BLOCK** — compare `nonblock_latency_mean`/`_p95`
  against `block_latency_mean`/`_p95`. A large, consistent gap quantifies the
  real-world cost of one strategy over the other on this hardware; a small
  gap means the choice is mostly a style preference here.
- **`avg_eagain_spins`** — has no BLOCK equivalent by design (the whole point
  of spin-polling is to avoid blocking, at the cost of these wasted checks).
  A high value means the non-blocking loop is burning CPU waiting; compare it
  against `nonblock_latency_mean` — high spins with low latency is expected,
  high spins **and** high latency suggests the spin loop itself is adding
  delay.
- **`*_captures` vs `30`** — a miss rate above what you see for
  [t03](t03-gpio-latency.md) under the same trigger mode suggests the I/O
  strategy itself (not the trigger) is the source of misses.

## 5. How the code works

`run_t04` runs two fully independent measurement blocks:

1. **NON_BLOCK block** — opens and starts a session (default non-blocking
   fd), warms up via `V4lSession::warmup()`, then for 30 samples: drains,
   waits 10 ms, fires the trigger, and **spin-polls** `VIDIOC_DQBUF` in a
   tight loop until it succeeds or a 100 ms deadline passes, counting
   `EAGAIN` spins along the way. A successful dequeue is timestamped against
   the trigger time and immediately requeued with `VIDIOC_QBUF`.
2. **BLOCK block** — opens a fresh session, explicitly clears `O_NONBLOCK`
   via `fcntl`, starts streaming, and runs 5 discarded warm-up cycles using
   `poll()` with a 200 ms timeout. Then for 30 samples: drains any
   already-ready frame with a zero-timeout `poll()` loop, waits 10 ms, fires
   the trigger, and dequeues once (blocking `DQBUF`, since the fd is now
   blocking), timestamping against the trigger.
3. **Aggregate** — for each mode with ≥1 captured frame, pushes its
   `compute_stats()`-derived `*_latency_*` family via `push_stats_metrics()`
   plus its capture count; NON_BLOCK additionally pushes `avg_eagain_spins`.
4. **Verdict** — `Fail` only if both latency vectors are empty; otherwise
   `Pass` with a summary reporting `captured/30` for each mode.

Both blocks measure latency with raw `clock_gettime()` timestamps around raw
`VIDIOC_DQBUF`/`VIDIOC_QBUF` ioctls rather than `V4lSession::capture()` —
the whole point of the test is to compare two different I/O strategies at
the ioctl level, so it cannot use the single blocking-with-poll strategy
that `capture()` implements internally.
