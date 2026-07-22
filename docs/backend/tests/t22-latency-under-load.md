# t22 — Latency under CPU load

> - **Implementation:** search `run_t22` in [diagnostic_runner.cpp](../../../source/backend/core/src/diagnostic_runner.cpp)
> - **Definition:** search `t22-latency-under-load` in [test_registry.cpp](../../../source/backend/core/src/test_registry.cpp)
> - **Category:** `stability`
> - **`uses_trigger`:** yes
> - **Trigger modes:** all (Hardware, Software, FreeRun)
> - Not in the original legacy diagnostic — added when the modular runner was built.

## 1. Overview / scope

**What it checks:** how much capture latency degrades when all CPU cores
are saturated with unrelated work, compared to an idle baseline.

**Questions it answers:**

- What is baseline capture latency when the system is otherwise idle?
- How much does that latency (mean and tail/p95) increase when 4 CPU-burn
  threads are running concurrently?
- Is the degradation small, moderate, or severe?

**Why it matters:** a real deployment rarely has the CPU to itself.
[t09](t09-sustained-capture.md) checks stability over *time* under normal
conditions; this test checks stability under *induced CPU contention*
specifically — the concrete question of "will this pipeline still work if
something else on the system is busy."

**Method:** measure a 30-sample baseline latency distribution with the CPU
idle, then start 4 CPU-saturating busy-loop threads and measure another
30-sample distribution while they run; compare the mean and p95 deltas.

## 2. Trigger modes

No restriction — CPU-load sensitivity is checked the same way regardless of
trigger source.

## 3. Inputs

All parameters are fixed in `run_t22`:

| Input | Value | Meaning |
| --- | --- | --- |
| Samples (baseline and under-load) | `30` each | Two separate 30-sample measurement passes. |
| Load threads | `4` | Busy-loop threads spawned between the baseline and under-load passes. |
| Buffer count | `2` | Both sessions. |
| Baseline capture timeout | `100 ms` | Per sample. |
| Under-load capture timeout | `200 ms` | Larger, since the system is under contention. |
| Sample interval | `200 ms` | Both passes. |
| Pass/Warn/Fail thresholds | `Δp95 < 5 ms` / `< 20 ms` / `>= 20 ms` | See Status below. |

> **Preconditions:** a streaming-capable camera and a working trigger for
> the selected mode. Enough CPU cores to meaningfully saturate with 4
> threads (on a system with fewer cores, the load may be less
> representative of "all cores busy").

## 4. Outputs & interpretation

### Metrics

| Metric | Unit | Meaning |
| --- | --- | --- |
| `baseline_latency_mean` / `_stddev` / `_min` / `_max` / `_p95` / `_jitter` | ms | Latency distribution measured with the CPU idle. |
| `baseline_captures` | count | Frames captured during the baseline pass. |
| `load_latency_mean` / `_stddev` / `_min` / `_max` / `_p95` / `_jitter` | ms | Latency distribution measured while 4 CPU-burn threads are running. Present only if ≥1 frame was captured under load. |
| `load_captures` | count | Frames captured during the under-load pass. |
| `delta_mean_ms` | ms | `load_latency_mean − baseline_latency_mean`. |
| `delta_p95_ms` | ms | `load_latency_p95 − baseline_latency_p95` — the metric the verdict is based on. |

### Status

| Status | Condition | Meaning |
| --- | --- | --- |
| `Pass` | `delta_p95_ms < 5.0` | CPU load has minimal impact on tail latency. |
| `Warn` | `5.0 <= delta_p95_ms < 20.0` | Moderate degradation under load. |
| `Fail` | `delta_p95_ms >= 20.0` | Severe tail-latency degradation under CPU contention. |
| `Fail` | zero baseline frames captured | No baseline data to compare against at all. |
| `Warn` | zero under-load frames captured (baseline succeeded) | Capture stopped working entirely once the CPU was saturated — a real finding, reported as `Warn` rather than `Fail` since it may reflect the test harness's own scheduling being starved rather than the capture pipeline itself. |

### How to read the numbers

- **`delta_p95_ms` is the primary verdict metric**, not `delta_mean_ms` —
  tail latency is what matters for a real-time pipeline; a stable mean with
  a growing p95 means occasional big stalls under load, which the mean
  alone would hide.
- **Compare `baseline_latency_*` here against
  [t03](t03-gpio-latency.md)'s numbers** for the same hardware/mode — they
  should be similar, since both measure idle-CPU trigger latency the same
  way; a large discrepancy would suggest something specific to this test's
  conditions rather than a real hardware difference.
- **Zero under-load captures** is itself informative — it means the
  pipeline could not keep up **at all** once 4 cores were saturated, which
  is a stronger signal than any `delta_p95_ms` value, even though it's
  reported as `Warn`.
- **Load thread count (`4`) is fixed** — on a system with many more cores,
  4 busy threads may not represent genuine full-system saturation; take
  the result in the context of the actual core count.

## 5. How the code works

`run_t22`:

1. **Baseline pass** — opens a session, starts streaming with `2` buffers,
   warms up, and captures 30 samples with a 100 ms timeout, 200 ms apart.
   Session failure yields `Error`; an empty baseline yields `Fail`
   ("No baseline frames").
2. **Spawn load threads** — starts 4 `std::thread`s, each spinning a tight
   `volatile` xorshift-style multiply-xor loop gated by a shared
   `std::atomic<bool> stop` flag, to saturate all CPU cores.
3. **Under-load pass** — opens a **fresh** session (independent of the
   baseline session, which has already been torn down), starts streaming,
   warms up, and captures 30 samples with a 200 ms timeout (larger than
   baseline's, since captures are competing with the load threads for CPU
   time).
4. **Stop load threads** — sets `stop = true` and joins all 4 threads
   before proceeding.
5. **Aggregate** — pushes `baseline_latency_*` and `baseline_captures`
   unconditionally; if the under-load pass captured anything, pushes
   `load_latency_*`, `load_captures`, `delta_mean_ms`, and `delta_p95_ms`.
6. **Verdict** — thresholds on `delta_p95_ms` if under-load data exists,
   otherwise `Warn` for zero under-load captures.

This is the only test in the suite that spawns worker threads to
deliberately create system-wide CPU contention as an experimental
condition — every other test measures the pipeline under otherwise-normal
system load.

This test is one of three added when the modular runner was built that
never existed in the original legacy diagnostic (alongside
[t21](t21-stuck-frame.md) and [t24](t24-control-inventory.md)) — see
[docs/testing.md](../../testing.md) for the full migration inventory.
