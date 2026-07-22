# t09 — Sustained capture stability

> - **Implementation:** search `run_t09` in [diagnostic_runner.cpp](../../../source/backend/core/src/diagnostic_runner.cpp)
> - **Definition:** search `t09-sustained-capture` in [test_registry.cpp](../../../source/backend/core/src/test_registry.cpp)
> - **Category:** `stability`
> - **`uses_trigger`:** yes
> - **Trigger modes:** all (Hardware, Software, FreeRun)
> - **Long-running:** yes — runs for a fixed 60 seconds

## 1. Overview / scope

**What it checks:** whether capture reliability and latency hold steady over
a full minute of continuous triggering, rather than just a short burst.

**Questions it answers:**

- Does the miss rate stay low across a sustained run, or does it degrade
  over time (thermal throttling, buffer leaks, driver drift)?
- Does average latency **drift** — is the second half of the run
  meaningfully slower than the first half?
- Are misses isolated, or do they cluster into long consecutive runs
  (a sign of a temporary stall vs. random noise)?

**Why it matters:** [t03](t03-gpio-latency.md)-style tests run for seconds,
not minutes — they can miss slow degradation that only shows up under
sustained load. This is the one test in the suite that is explicitly
**time-bounded** rather than sample-count-bounded.

**Method:** capture continuously for 60 seconds at roughly 10 Hz, split into
six 10-second windows; track overall success rate, the longest run of
consecutive misses, and compare the first half of windows' mean latency
against the second half's to compute drift.

## 2. Trigger modes

No restriction — sustained stability is checked the same way regardless of
trigger source.

## 3. Inputs

All parameters are fixed in `run_t09`:

| Input | Value | Meaning |
| --- | --- | --- |
| Total duration | `60 s` | Wall-clock, measured with `steady_clock`. |
| Window size | `10 s` | The run is split into 6 windows for drift analysis. |
| Capture timeout | `100 ms` | Per sample. |
| Sample interval | `100 ms` | Between captures (~10 Hz target rate). |
| Buffer count | `2` | |

> **Preconditions:** a streaming-capable camera and a working trigger for the
> selected mode. `s.warmup(trigger)` runs once before measurement begins.

## 4. Outputs & interpretation

### Metrics

| Metric | Unit | Meaning |
| --- | --- | --- |
| `frames_captured` | count | Total successful captures over the full 60 s. |
| `success_rate_pct` | % | `frames_captured / (frames_captured + misses) × 100`. |
| `max_consecutive_miss` | count | Longest unbroken run of misses observed. |
| `latency_mean` / `_stddev` / `_min` / `_max` / `_p95` / `_jitter` | ms | Latency distribution over **all** captured frames in the run (present only if ≥1 frame captured). |
| `latency_drift_ms` | ms | Mean of the second half of windows' means minus the mean of the first half's — positive means latency got worse over the run. |

`details` gets one line per completed 10 s window: sample count, mean,
stddev, and miss count for that window specifically.

### Status

| Status | Condition | Meaning |
| --- | --- | --- |
| `Pass` | `success_rate_pct >= 95` **and** `\|drift\| < 1.0 ms` | Reliable and stable for the full minute. |
| `Warn` | `success_rate_pct >= 80` **and** `\|drift\| < 5.0 ms` | Degraded but not badly — either the miss rate or the drift is outside the strict Pass band. |
| `Fail` | neither condition met | Either too many misses, or latency drifted by ≥5 ms across the run. |
| `Error` | session open/start failed | Setup problem, not a result (see summary). |

### How to read the numbers

- **`success_rate_pct` alone doesn't tell you about clustering** — check
  `max_consecutive_miss` too. A 95% success rate made of one 5%-long stall
  is a very different problem than 5% of misses spread randomly across the
  minute.
- **`latency_drift_ms`** is the key sustained-load signal that shorter tests
  cannot see: a small mean/stddev with a large positive drift means the
  pipeline starts fine but degrades — worth correlating with CPU temperature
  or memory pressure over the same window. Compare against
  [t22](t22-latency-under-load.md), which measures a similar delta but
  under **deliberately induced** CPU load rather than sustained normal
  operation.
- **Per-window `details` lines** — useful to spot exactly *when* in the
  60 seconds a stall or drift began, rather than only seeing the aggregate.

## 5. How the code works

`run_t09`:

1. **Open + start + warm-up** — opens the device, starts streaming with `2`
   buffers, and warms up. Failure yields `Error`.
2. **Time-bounded loop** — unlike sample-count-bounded tests, this loop
   checks elapsed wall-clock time (`steady_clock`) against the 60 s
   duration on every iteration, not a fixed sample count.
3. **Window rollover** — whenever the elapsed time crosses into a new 10 s
   window, the just-finished window's `compute_stats()` mean is recorded
   into `win_means`, a details/log line is emitted for it, and the
   per-window accumulators reset.
4. **Capture** — each iteration captures with a 100 ms timeout; on success,
   appends to both the overall and current-window latency vectors and
   resets the consecutive-miss counter; on failure, increments total/window
   miss counts and tracks the longest consecutive-miss streak.
5. **Aggregate** — computes the overall success rate, pushes
   `frames_captured`/`success_rate_pct`/`max_consecutive_miss`, and — if any
   frames were captured — the `latency_*` family via `push_stats_metrics()`.
6. **Drift** — if at least 2 windows completed, splits `win_means` in half
   and computes `(second-half average) - (first-half average)` as
   `latency_drift_ms`.
7. **Verdict** — thresholds on `success_rate_pct` and `|drift|` as described
   above.

This is the only test whose outer loop condition is elapsed time rather than
a sample counter — every other test in the suite runs a fixed number of
iterations.
