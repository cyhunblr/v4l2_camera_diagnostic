# t10 — Multi-buffer configurations

> - **Implementation:** search `run_t10` in [diagnostic_runner.cpp](../../../source/backend/core/src/diagnostic_runner.cpp)
> - **Definition:** search `t10-multi-buffer` in [test_registry.cpp](../../../source/backend/core/src/test_registry.cpp)
> - **Category:** `buffering`
> - **`uses_trigger`:** no (registry) — but takes an **optional** trigger and uses it if available
> - **Trigger modes:** all (the mode does not restrict this test either way)

## 1. Overview / scope

**What it checks:** how many buffers `VIDIOC_REQBUFS` actually grants when 1
through 5 are requested, and — only if a trigger happens to be available —
capture latency at each buffer count.

**Questions it answers:**

- Does the driver grant exactly the number of buffers requested, or does it
  clamp/round to some other value?
- Does capture latency change noticeably as buffer count increases?

**Why it matters:** every other test in this suite hardcodes `2` buffers.
This is the one test that actually varies the buffer count and checks what
the driver grants — useful when tuning buffer count for a real deployment.

**Method:** for buffer counts 1 through 5, open a session, request that many
buffers via `VIDIOC_REQBUFS`, record how many were granted, release them, and
— if a trigger source is available — start streaming at that buffer count
and capture 20 samples to see whether latency changes with buffer count.

## 2. Trigger modes

This test is declared `uses_trigger: false` in the registry, so **no trigger
mode restricts it** and `run_test()` runs it even when no trigger is
available for the selected mode. Internally, though, `run_t10` accepts a
**nullable** trigger pointer:

- **Trigger available** (any mode with a working channel) — the REQBUFS
  probe runs as normal, and additionally each buffer count is actually
  streamed and captured against, producing latency data per count.
- **Trigger unavailable** (e.g. no channel configured, or FreeRun with
  session issues) — the REQBUFS probe still runs and reports granted buffer
  counts; the capture/latency portion is simply skipped for every buffer
  count, and `details` notes "capture skipped" per count.

This makes `t10` unique in the suite: it is the only test that adapts its own
scope at runtime based on whether a trigger happens to be present, rather
than being skipped or requiring one.

## 3. Inputs

All parameters are fixed in `run_t10`:

| Input | Value | Meaning |
| --- | --- | --- |
| Buffer counts swept | `1, 2, 3, 4, 5` | Requested via `VIDIOC_REQBUFS` in turn. |
| Samples per count | `20` | Only captured if a trigger is available. |
| Warm-up | `3` triggers @ `200 ms` | Only when a trigger is available, before each count's capture loop. |
| Capture timeout | `100 ms` | Per sample. |
| Sample interval | `200 ms` | Between captures. |

> **Preconditions:** none beyond an openable V4L2 device — a trigger is
> optional, not required.

## 4. Outputs & interpretation

### Metrics

| Metric | Unit | Meaning |
| --- | --- | --- |
| `granted_for_1` … `granted_for_5` | count | Buffers actually granted by `VIDIOC_REQBUFS` when that many were requested. |

`details` gets one line per buffer count: the granted count, and — when a
trigger was available — the mean latency and miss count for that count's
capture loop (or a note that capture was skipped).

### Status

| Status | Condition | Meaning |
| --- | --- | --- |
| `Pass` | always | This test characterizes buffer-grant behavior; it never asserts a specific count is "correct". |

There is no `Fail`/`Warn`/`Error` path other than per-count setup failures,
which are recorded in `details` and simply skip that count (`continue`) —
they do not fail the overall test.

### How to read the numbers

- **`granted_for_N == N`** for all `N` — the expected, healthy case: the
  driver grants exactly what is requested.
- **`granted_for_N != N`** — the driver clamped or rounded the request. Not
  necessarily a bug (some drivers have a fixed minimum), but worth noting
  when tuning buffer count for production.
- **Per-count latency in `details`** (when present) — compare across buffer
  counts to see whether more buffers measurably help or hurt latency on this
  hardware; a flat result across counts means buffer count is not the
  latency-limiting factor here.
- **"capture skipped" notes** — if every count shows this, no trigger was
  available for the run; only the REQBUFS-grant data is meaningful for that
  run.

## 5. How the code works

`run_t10`:

1. **Per-count loop** (`1` through `5`): opens a fresh session for each
   count. A device-open failure is recorded in `details` and that count is
   skipped (`continue`), not treated as a whole-test error.
2. **Probe REQBUFS** — issues a raw `VIDIOC_REQBUFS` request for the current
   count, reads back `req.count` as the granted amount, and immediately
   releases the buffers (`req.count = 0`) so the next iteration starts
   clean. Pushes `granted_for_<count>`.
3. **Conditional capture** — only if `trigger` is non-null: starts streaming
   at this buffer count, warms up with 3 triggers, then captures 20 samples,
   recording mean latency and miss count into `details`. If `trigger` is
   null, appends a "GPIO unavailable, capture skipped" detail line instead.
4. **Verdict** — always `Pass`; the summary notes whether latency data was
   collected or only the buffer-grant probe ran.

`run_t10` is the only function in the suite taking `TriggerSource *` instead
of `TriggerSource &` — every other test either requires a trigger (skipped
by `run_test()` if unavailable) or fires none at all. This one explicitly
adapts to whichever is true at runtime.
