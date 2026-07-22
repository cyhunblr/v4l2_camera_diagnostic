# t05 — Poll timeout effect

> - **Implementation:** search `run_t05` in [diagnostic_runner.cpp](../../../source/backend/core/src/diagnostic_runner.cpp)
> - **Definition:** search `t05-poll-timeout-effect` in [test_registry.cpp](../../../source/backend/core/src/test_registry.cpp)
> - **Category:** `polling`
> - **`uses_trigger`:** yes
> - **Trigger modes:** all (Hardware, Software, FreeRun)

## 1. Overview / scope

**What it sweeps:** the `poll()`/capture timeout passed per sample, across a
fixed set of representative values, to find where misses start appearing.

**Questions it answers:**

- At what capture timeout does the pipeline start missing frames?
- What is the highest timeout that is still completely miss-free?

**Why it matters:** every caller has to pick a capture timeout. Too low and
frames are missed even though the hardware is fine; too high and a real stall
takes longer to detect. This test finds the actual cliff for the current
hardware and trigger interval instead of guessing.

**Method:** sweep 14 representative timeout values from `1 ms` to `500 ms`;
for each, capture 20 samples and count misses; stop the sweep early once a
value misses every sample — the pipeline cannot keep up even at that
timeout, so higher values are unlikely to fare better.

## 2. Trigger modes

No restriction — the sweep applies to any trigger source; latency and miss
behavior are measured the same way regardless of mode.

## 3. Inputs

All parameters are fixed in `run_t05`:

| Input | Value | Meaning |
| --- | --- | --- |
| Timeout sweep | `1, 2, 5, 10, 20, 30, 40, 45, 48, 50, 55, 60, 100, 500` ms (14 values) | Ascending; denser near the expected cliff (40–60 ms). |
| Samples per timeout | `20` | Captures attempted at each timeout value. |
| Buffer count | `2` | |
| Sample interval | `200 ms` | Between captures, at every timeout value. |
| Early exit | when a timeout value misses all 20 samples | Stops the sweep — higher timeouts already covered lower ones' ground. |

> **Preconditions:** a streaming-capable camera and a working trigger for the
> selected mode. `s.warmup(trigger)` runs once before the sweep starts.

## 4. Outputs & interpretation

### Metrics

| Metric | Unit | Meaning |
| --- | --- | --- |
| `cliff_timeout_ms` | ms | First swept timeout value that produced ≥1 miss. `-1` if none did. |
| `last_clean_timeout_ms` | ms | Highest swept timeout value with **zero** misses. `-1` if every value missed at least once. |

`details` records one line per timeout value with the mean latency and
miss count (or "ALL MISSED").

### Status

| Status | Condition | Meaning |
| --- | --- | --- |
| `Pass` | `last_clean_timeout_ms >= 0` | At least one timeout value was completely miss-free (whether or not a cliff was also found). |
| `Fail` | `last_clean_timeout_ms < 0` | Every single timeout value — including `500 ms` — missed at least one sample. This points at a trigger or capture-path problem, not a timeout-tuning problem. |
| `Error` | session open/start failed | Setup problem, not a result (see summary). |

### How to read the numbers

- **`cliff_timeout_ms` vs `last_clean_timeout_ms`** — the healthy pattern is a
  cliff a little above the last-clean value, e.g. `last_clean=40, cliff=45`:
  misses appear right where you'd expect, close to the frame period. A large
  gap between them (e.g. `last_clean=10, cliff=100`) suggests intermittent,
  non-deterministic misses rather than a clean timeout boundary.
- **`cliff_timeout_ms == -1`** — no timeout in the swept range produced a
  miss, i.e. the pipeline is reliable even at `1 ms`. Unusual, but not a
  problem.
- **Relative to your frame period** `T = 1000 / fps` — the cliff should sit
  near `T`. A cliff far below `T` (e.g. `10 ms` cliff at 30 fps, `T≈33 ms`)
  means the driver is slower than expected to deliver frames; a cliff far
  above `T` suggests other tests' assumptions about timeout margins (see
  [t07](t07-poll-timeout-sweep.md), which hardcodes a `48 ms` production
  timeout) may need revisiting for this specific hardware.

## 5. How the code works

`run_t05`:

1. **Open + start + warm-up** — opens the device, starts streaming with `2`
   buffers, and runs the default `warmup()`. Failure yields `Error`.
2. **Sweep loop** — for each of the 14 timeout values, in ascending order:
   captures 20 samples via `s.capture(trigger, tms)`, recording latency on
   success and counting misses on failure, with a 200 ms pause between
   samples. Records the first timeout with a miss as `cliff_ms` (only the
   first time it happens) and updates `last_clean_ms` whenever a value has
   zero misses. Breaks out of the sweep entirely if a value misses all 20
   samples — nothing below that point is going to look different, and the
   pipeline has effectively failed at this timeout.
3. **Verdict** — `Fail` only if `last_clean_ms` was never set (every tested
   value had at least one miss); otherwise `Pass`, with a summary describing
   either "no misses at any value" or the cliff/last-clean pair.

Unlike [t03](t03-gpio-latency.md) or [t04](t04-nonblock-vs-block.md), this
test does not push per-timeout latency stats as metrics — only the two cliff
scalars are recorded as metrics, with per-timeout means available in
`details` for manual inspection.
