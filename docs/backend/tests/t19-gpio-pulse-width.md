# t19 — GPIO pulse width characterization

> - **Implementation:** search `run_t19` in [diagnostic_runner.cpp](../../../source/backend/core/src/diagnostic_runner.cpp)
> - **Definition:** search `t19-gpio-pulse-width` in [test_registry.cpp](../../../source/backend/core/src/test_registry.cpp)
> - **Category:** `trigger`
> - **`uses_trigger`:** yes
> - **Trigger modes:** Hardware only

## 1. Overview / scope

**What it sweeps:** the GPIO trigger pulse width, from `1 ms` to `30 ms`,
measuring capture latency relative to both the pulse's rising (HIGH) edge
and an approximated falling (LOW) edge — to infer which edge the sensor
actually triggers on.

**Questions it answers:**

- Does capture latency stay stable across different pulse widths, or does it
  depend on pulse duration?
- Does the sensor trigger on the rising edge of the GPIO pulse, the falling
  edge, or is it inconclusive from the data?

**Why it matters:** [t03](t03-gpio-latency.md) measures latency using a
fixed default pulse width; this test instead varies the pulse width itself
and uses the resulting latency pattern to infer trigger edge polarity — a
question about the hardware's actual trigger behavior, not about the
software pipeline.

**Method:** for 11 pulse widths, fire 8 triggered captures each, measuring
latency from the pulse's HIGH-edge timestamp and from an approximated
LOW-edge timestamp; if latency-from-HIGH is meaningfully more stable across
widths than latency-from-LOW (or vice versa), that indicates which edge the
sensor is actually responding to.

## 2. Trigger modes

Mask `0x01` — **Hardware only**. This test explicitly characterizes the GPIO
pulse itself, so it does not apply to Software (V4L2-control-based) or
FreeRun triggering — there is no GPIO pulse to vary in either of those
modes.

## 3. Inputs

All parameters are fixed in `run_t19`:

| Input | Value | Meaning |
| --- | --- | --- |
| Pulse widths swept | `1, 2, 3, 5, 7, 10, 13, 15, 20, 25, 30` ms (11 values) | Passed as `pulse_ns` to `trigger.send()`. |
| Samples per width | `8` | Captures attempted at each pulse width. |
| Warm-up | `5` triggers @ `200 ms` | Before the sweep starts. |
| Buffer count | `2` | |
| Poll timeout | `500 ms` | Per sample — generous, since this test is characterizing timing, not stressing it. |
| Pre-trigger settle | `10 ms` | After draining, before each trigger. |

> **Preconditions:** a streaming-capable camera with a working **Hardware**
> GPIO trigger channel.

## 4. Outputs & interpretation

### Metrics

| Metric | Unit | Meaning |
| --- | --- | --- |
| `hits_<width>ms` | count | Successful captures out of 8, at that pulse width (one per swept width). |
| `lat_high_avg_<width>ms` | ms | Mean latency measured from the pulse's **HIGH**-edge timestamp, at that width (present only if `hits > 0`). |
| `lat_low_avg_<width>ms` | ms | Mean latency measured from the pulse's approximated **LOW**-edge timestamp, at that width (present only if `hits > 0`). |
| `range_h` | ms | Average within-width `(max − min)` spread of HIGH-edge latency, across widths that got a full 8/8 hits. |
| `range_l` | ms | Same, for LOW-edge latency. |

`details` gets one line per pulse width with hit count and both mean
latencies (or "all missed"), plus an "Edge detection" line.

### Status

| Status | Condition | Meaning |
| --- | --- | --- |
| `Pass` | always | This test characterizes trigger-edge behavior; it never asserts one edge is "correct". |
| `Error` | session open/start failed | Setup problem, not a result (see summary). |

There is no `Fail`/`Warn` — trigger edge polarity is a hardware property to
discover, not a pass/fail condition.

### How to read the numbers

- **The summary's "Trigger edge" verdict** (`rising`, `falling`, or
  `inconclusive`) is the headline result — see §5 for exactly how it's
  derived from `range_h`/`range_l`.
- **`range_h` much smaller than `range_l`** (edge = "rising") means latency
  measured from the HIGH edge is consistently tighter across pulse widths —
  i.e. the sensor's actual trigger point tracks the rising edge, and
  measuring from the LOW edge adds noise proportional to pulse width
  variation (as expected, since the LOW edge timestamp *is* pulse-width
  dependent by construction).
- **`hits_<width>ms` dropping at very short widths** (e.g. `1ms`, `2ms`)
  suggests the sensor or GPIO driver needs a minimum pulse width to
  reliably register a trigger — compare against your hardware's documented
  minimum pulse width, if any.
- **"inconclusive"** — happens when no pulse width achieved a full 8/8 hit
  rate (so no `range_h`/`range_l` could be computed) or when the two ranges
  are within `1.0 ms` of each other, too close to call.

## 5. How the code works

`run_t19`:

1. **Open + start + warm-up** — opens the device, starts streaming with `2`
   buffers, and warms up with 5 triggers. Failure yields `Error`.
2. **Per-width loop** (11 pulse widths): for each of 8 samples, drains,
   waits 10 ms, then fires `trigger.send(pw_ns)` — recording the returned
   HIGH-edge timestamp. An **approximated** LOW-edge timestamp is computed
   by adding the pulse width to the HIGH-edge time (i.e. estimating when the
   pulse would return low, not measured directly). Polls up to 500 ms for a
   frame; on success, computes latency from **both** the HIGH-edge and the
   approximated LOW-edge timestamps.
3. **Per-width aggregation** — pushes `hits_<width>ms`, and (if any hits)
   the two mean-latency metrics plus a details line. If the width achieved a
   full 8/8 hit rate, its HIGH/LOW latency **ranges** (max−min within that
   width) are accumulated for the edge-detection step.
4. **Edge inference** — averages the accumulated HIGH and LOW ranges across
   all full-hit-rate widths into `range_h`/`range_l`. If `range_h` is more
   than `1.0 ms` tighter than `range_l`, concludes "rising"; if `range_l` is
   tighter, concludes "falling"; otherwise "inconclusive".
5. **Verdict** — always `Pass`, with the inferred edge in the summary.

The LOW-edge timestamp here is **derived arithmetically** as
`t_high + pulse_width`, not independently measured. This makes the edge
inference work as a subtraction trick: if the sensor actually triggers on
the HIGH edge, `latency_from_HIGH` stays roughly constant across pulse
widths (it measures from the real trigger point), while `latency_from_LOW`
systematically shrinks as pulse width grows (since a growing offset is being
subtracted from the same real trigger point) — producing a **wider**
`range_l` than `range_h` across the swept widths. If the sensor instead
triggers on the falling edge, the pattern reverses. The `1.0 ms` threshold
between `range_h` and `range_l` is what the edge-detection logic checks for.
