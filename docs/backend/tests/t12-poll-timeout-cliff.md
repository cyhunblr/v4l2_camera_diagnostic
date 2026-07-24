# t12 — Poll Timeout Cliff Finder

**Layer:** 4 — Polling / timeout  
**Category:** polling  
**Trigger required:** yes  

## Purpose

Finds the minimum stable poll timeout at which the camera pipeline delivers frames without misses. The "cliff" is the boundary below which captures begin to fail — knowing this value is critical for setting production timeout values that are both responsive and reliable. The test also computes a safety margin relative to the configured production timeout, flagging configurations that are dangerously close to the cliff.

## How It Works

1. Opens the camera device, starts streaming, and warms up with 10 captures at 200 ms intervals.
2. **Phase 1 — Coarse search**: Confirms a high timeout (200 ms or 500 ms) delivers 100% hits, then steps through decreasing timeout values (150, 100, 80, 60, 50, 40, 30, 20, 15, 10, 7, 5, 3, 2, 1 ms) until misses are observed.
3. **Phase 2 — Binary search**: Narrows the cliff to a 1 ms resolution between the last clean timeout (`lo_ms`) and the first miss timeout (`hi_ms`).
4. **Phase 3 — Stability confirmation**: Over `stability_rounds` rounds, confirms the cliff is repeatable by verifying 100% hits at the cliff and less-than-100% hits one millisecond below.
5. Computes the safety margin as `production_timeout_ms - cliff_ms`.

## Implementation

Function: `run_poll_timeout_cliff` in [diagnostic_runner.cpp](../../../source/backend/core/src/diagnostic_runner.cpp)  
Registry: `t12-poll-timeout-cliff` in [test_registry.cpp](../../../source/backend/core/src/test_registry.cpp)

> The source file contains `// Docs: docs/backend/tests/t12-poll-timeout-cliff.md`
> above the function as a back-reference to this document.

## Parameters

| Key | Default | Unit | Description |
| ----- | --------- | ------ | ------------- |
| `probe_frames` | 10 | count | Frames per probe at each timeout value |
| `stability_rounds` | 5 | count | Rounds of stability confirmation |
| `stability_frames` | 10 | count | Frames per stability round |

## Output Metrics

| Metric Key | Unit | Description |
| ----------- | ------ | ------------- |
| `cliff_ms` | ms | Lowest timeout with 100% capture success (-1 if no cliff found or pipeline unreliable) |
| `first_miss_ms` | ms | Highest timeout value where misses occurred |
| `safety_margin_ms` | ms | `production_timeout_ms` minus `cliff_ms` |
| `stability_confirmed` | bool | 1.0 if cliff was stable over the required number of rounds |
| `stability_rounds_passed` | count | Number of stability rounds that confirmed the cliff (out of `stability_rounds`) |

## Report Details

The test emits detail lines for each probed timeout and stability round:

```text
coarse: 100ms → 10/10
coarse:  50ms → 10/10
coarse:  40ms → 10/10
coarse:  30ms → 8/10
bsearch:  35ms → 10/10
bsearch:  32ms → 10/10
bsearch:  31ms → 9/10
stability round 1: @32ms=10/10, @31ms=7/10 ✓
stability round 2: @32ms=10/10, @31ms=8/10 ✓
stability round 3: @32ms=10/10, @31ms=6/10 ✓
stability round 4: @32ms=10/10, @31ms=9/10 ✓
stability round 5: @32ms=10/10, @31ms=5/10 ✓
```

A summary box is also emitted to the log:

```text
╔═══════ CLIFF SUMMARY ══════════╗
║  Production timeout :  48.5ms  ║
║  Cliff (stable)     :    32ms  ║
║  Safety margin      :  16.5ms  ║
║  Stability          :   5/5    ║
║  Confirmed          :   YES    ║
╚════════════════════════════════╝
```

## Verdict Logic

| Status | Condition |
| -------- | ----------- |
| **Pass** | Cliff is stable AND `safety_margin_ms >= safe_margin_ms` threshold (default: 5.0 ms) |
| **Pass** | No cliff found — pipeline is reliable even at 1 ms timeout |
| **Warn** | Cliff is stable but safety margin is less than threshold (but still ≥ 0) |
| **Warn** | Cliff is unstable (fewer than 3/5 stability rounds confirmed) |
| **Fail** | Cliff is above the production timeout (`safety_margin_ms < 0`) |
| **Fail** | Pipeline misses even at 500 ms — no cliff can be found |

**Thresholds from `default_threshold_config`:**

| Key | Default | Description |
| --- | ------- | ----------- |
| `production_timeout_ms` | 48.5 | The production poll timeout to compare against |
| `safe_margin_ms` | 5.0 | Minimum acceptable safety margin for Pass |

## Interpretation Guide

- **cliff_ms = -1, safety_margin = production - 1**: Pipeline never misses — excellent. Even 1 ms timeouts succeed.
- **safety_margin > 10 ms**: Comfortable margin. Production timeout is well above the cliff.
- **safety_margin 0–5 ms**: Dangerously close. Temperature changes or system load spikes could push past the cliff.
- **safety_margin < 0**: Production timeout is below the cliff — captures will fail in production.
- **stability_confirmed = false**: Cliff position is not repeatable — pipeline has variable latency; the cliff may shift under load.

## Failure Modes

| Symptom | Likely Cause |
| --------- | -------------- |
| "Misses even at 500ms timeout" | Camera not triggering, I2C bus error, or driver/firmware hang |
| Unstable cliff (fluctuates between rounds) | Thermal throttling, kernel scheduling jitter, or trigger source instability |
| Cliff very high (> 100 ms) | Camera in a slow readout mode, low frame rate configuration, or long integration time |
| Cliff exactly at 1 ms | Possible integer rounding in the driver's poll implementation |
