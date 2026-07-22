# t07 — Poll timeout sweep (200ms → 1ms)

> - **Implementation:** search `run_t07` in [diagnostic_runner.cpp](../../../source/backend/core/src/diagnostic_runner.cpp)
> - **Definition:** search `t07-poll-timeout-sweep` in [test_registry.cpp](../../../source/backend/core/src/test_registry.cpp)
> - **Category:** `polling`
> - **`uses_trigger`:** yes
> - **Trigger modes:** all (Hardware, Software, FreeRun)

## 1. Overview / scope

**What it checks:** whether the pipeline's timeout cliff leaves enough safety
margin below a specific **production timeout** value.

**Questions it answers:**

- At exactly what timeout does the pipeline start missing frames?
- Is that cliff far enough below the timeout the application actually uses in
  production, or is it uncomfortably close (or above it)?

**Why it matters:** this is a pass/fail safety check, not just a
characterization. It answers "is our chosen production timeout safe" with a
concrete margin in milliseconds, using a finer-grained sweep than
[t05](t05-poll-timeout-effect.md).

**How this differs from t05:** [t05](t05-poll-timeout-effect.md) sweeps 14
representative values ascending and never fails (it just reports where the
cliff is). `t07` sweeps **every** value from 200 ms down to 1 ms — much finer
resolution — and turns the result into a **Pass/Warn/Fail verdict** against a
hardcoded production timeout.

**Method:** starting at 200 ms and stepping down to 1 ms, capture 10 samples
at each timeout; find where hits stop being 100%, then compare that cliff
against the production timeout to compute a safety margin.

## 2. Trigger modes

No restriction — applies the same way regardless of trigger source.

## 3. Inputs

All parameters are fixed in `run_t07`:

| Input | Value | Meaning |
| --- | --- | --- |
| Timeout sweep | `200, 190, ..., 10` (step −10), then `9, 8, ..., 1` (step −1) | Descending, fine-grained near the low end. |
| Frames per timeout | `10` | Samples captured at each timeout value. |
| Buffer count | `2` | |
| Warm-up | `10` triggers @ `200 ms` | Before the sweep starts. |
| Sample interval | `200 ms` | Between captures, at every timeout value. |
| Production timeout | `48.5 ms` | Hardcoded reference value the cliff is judged against. |
| Safe margin threshold | `≥ 5.0 ms` | Safety margin needed for a `Pass`. |

> **Preconditions:** a streaming-capable camera and a working trigger for the
> selected mode.

## 4. Outputs & interpretation

### Metrics

| Metric | Unit | Meaning |
| --- | --- | --- |
| `last_clean_ms` | ms | Lowest timeout tested with zero misses (0% miss rate). |
| `first_miss_ms` | ms | First (highest) timeout tested with at least one miss. |
| `last_alive_ms` | ms | Lowest timeout tested with at least one successful capture. |
| `first_dead_ms` | ms | First (highest) timeout tested with a 100% miss rate. |
| `safety_margin_ms` | ms | `48.5 − last_clean_ms`. Positive means the production timeout has headroom above the cliff. |

The test also emits a human-readable box-drawn "SWEEP SUMMARY" to the log
(production timeout, lowest 0%-miss value, first miss, safety margin) — this
is a log convenience, not a stored metric. `details` records one line per
timeout value with hit count and mean latency (when any frame succeeded).

### Status

| Status | Condition | Meaning |
| --- | --- | --- |
| `Pass` | `safety_margin_ms >= 5.0` | The cliff sits comfortably below the production timeout. |
| `Warn` | `0.0 <= safety_margin_ms < 5.0` | The cliff is below the production timeout, but the margin is thin. |
| `Fail` | `safety_margin_ms < 0.0` | The cliff sits **above** the production timeout — the timeout the application actually uses would already be missing frames. |
| `Error` | session open/start failed | Setup problem, not a result (see summary). |

### How to read the numbers

- **This is a go/no-go check for one specific number** (48.5 ms), not a
  general characterization — if your application uses a different production
  timeout, `last_clean_ms`/`first_miss_ms` are the values to compare it
  against manually; `safety_margin_ms` itself is only meaningful for the
  hardcoded 48.5 ms reference.
- **`Fail` here is a real signal**, unlike most other tests in this suite:
  it means the specific timeout this system is built around is already past
  the cliff.
- **`last_clean_ms` vs `first_dead_ms`** — a large gap between them (many
  values with partial misses) suggests a soft, gradual cliff; a small or zero
  gap suggests a sharp, deterministic cutoff.

## 5. How the code works

`run_t07`:

1. **Open + start + warm-up** — opens the device, starts streaming with `2`
   buffers, and warms up with 10 triggers at 200 ms spacing. Failure yields
   `Error`.
2. **Build the descending sweep** — `200, 190, ..., 10` then `9, 8, ..., 1`
   (30 values total), finer-grained than [t05](t05-poll-timeout-effect.md)'s
   14-value sweep.
3. **Sweep loop** — for each timeout, captures 10 samples via
   `s.capture(trigger, tms)`, tracking hits/misses; updates `last_clean`
   (still 0 misses), `first_miss` (first time any miss appears),
   `last_alive` (still ≥1 hit), and `first_dead` (first 100%-miss value).
   Logs a `✓`/`⚠`/`✖` marker per timeout depending on the miss rate.
4. **Compute + report** — computes `safety = 48.5 − last_clean`, emits the
   box-drawn summary via `emit_data`, and pushes all five metrics.
5. **Verdict** — `Pass` if `safety >= 5.0`, `Warn` if `0 <= safety < 5.0`,
   `Fail` if `safety < 0`.

Unlike [t05](t05-poll-timeout-effect.md), this test never pushes per-timeout
latency stats to metrics (only cliff-related scalars) and is the only sweep
test that produces a `Fail` based on a fixed external reference value rather
than the sweep's own internal consistency.
