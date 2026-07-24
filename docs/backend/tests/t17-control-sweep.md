# t17 — Control Parameter Sweep

**Layer:** 5 — Latency  
**Category:** controls  
**Trigger required:** yes  

## Purpose

Enumerates all V4L2 controls on the device and, when ISX021-specific controls are detected, performs a combinatorial sweep of the LED_LEVEL, BYPASS, and WINDOW_INTEG controls to measure how each combination affects capture latency. This produces a latency profile for each ISX021 configuration state, enabling optimal control selection for the target latency budget.

## How It Works

1. Opens the device and iterates all V4L2 controls using `VIDIOC_QUERYCTRL` with `V4L2_CTRL_FLAG_NEXT_CTRL`.
2. For each non-disabled control, records: name, hex ID, min/max/step/default values, current value, and read-only status.
3. Counts total controls and writable controls; detects presence of ISX021-specific control IDs (LED_LEVEL=0x9a206d, BYPASS=0x9a2064, WINDOW_INTEG=0x9a2068).
4. If ISX021 controls are found:
   a. Saves original control values.
   b. Iterates all 8 combinations of {LED_LEVEL, BYPASS, WINDOW_INTEG} × {0, 1}.
   c. For each combination: sets the controls, opens a fresh session, warms up (8 captures), captures 20 frames, and records mean latency.
   d. Restores original control values.
5. Reports the full control inventory in details and per-combination latency as metrics.

## Implementation

Function: `run_control_sweep` in [diagnostic_runner.cpp](../../../source/backend/core/src/diagnostic_runner.cpp)  
Registry: `t17-control-sweep` in [test_registry.cpp](../../../source/backend/core/src/test_registry.cpp)

> The source file contains `// Docs: docs/backend/tests/t17-control-sweep.md`
> above the function as a back-reference to this document.

## Parameters

| Key | Default | Unit | Description |
|-----|---------|------|-------------|
| `warmup_count` | 8 | count | Warmup captures per combination |
| `sample_count` | 20 | count | Captures per combination for latency measurement |
| `capture_timeout_ms` | 200 | ms | Poll timeout per capture attempt |

## Output Metrics

### Inventory metrics

| Metric Key | Unit | Description |
|-----------|------|-------------|
| `control_count` | count | Total number of non-disabled V4L2 controls |
| `writable_count` | count | Number of writable (non-read-only) controls |
| `isx021_found` | bool | 1.0 if ISX021-specific controls (LED_LEVEL, BYPASS, WINDOW_INTEG) are present |

### ISX021 sweep metrics (only when isx021_found = 1.0)

For each combination of ll∈{0,1}, bp∈{0,1}, wi∈{0,1}:

| Metric Key Pattern | Unit | Description |
|-------------------|------|-------------|
| `ll{0\|1}_bp{0\|1}_wi{0\|1}_mean_ms` | ms | Mean capture latency for this control combination |

Example metric keys: `ll0_bp0_wi0_mean_ms`, `ll0_bp0_wi1_mean_ms`, `ll0_bp1_wi0_mean_ms`, ..., `ll1_bp1_wi1_mean_ms`

## Report Details

### Control inventory (one line per control)

```
exposure_absolute [0x009a0902] min=1 max=10000 step=1 def=166 cur=166
gain [0x009a0914] min=0 max=255 step=1 def=0 cur=0
isx021_led_level [0x009a206d] min=0 max=1 step=1 def=0 cur=0
isx021_bypass [0x009a2064] min=0 max=1 step=1 def=0 cur=0
isx021_window_integ [0x009a2068] min=0 max=1 step=1 def=0 cur=0 [RO]
```

### ISX021 sweep results

```
ll0_bp0_wi0: n=20 mean=34.5ms
ll0_bp0_wi1: n=18 mean=36.2ms
ll0_bp1_wi0: n=20 mean=33.1ms
...
```

## Verdict Logic

| Status | Condition |
|--------|-----------|
| **Pass** | Control enumeration succeeds (with or without ISX021 sweep) |
| **Fail** | Cannot open the device |

This test always passes if the device can be opened. It is a characterization/inventory test — no thresholds are applied to latency values.

## Interpretation Guide

- **isx021_found = 0**: Camera is not ISX021 or does not expose ISX021-specific controls. Only the inventory is reported.
- **Significant latency variation across combinations**: Certain control states enable faster readout or bypass ISP processing stages.
- **ll1 combinations consistently faster**: LED_LEVEL=1 reduces integration/readout time.
- **bp1 combinations faster**: BYPASS mode skips ISP processing stages, reducing latency.
- **Some combinations have fewer captures (n < 20)**: That control combination causes occasional frame drops or timeout.
- **writable_count = 0**: All controls are read-only — cannot tune camera behavior via V4L2 controls.

## Failure Modes

| Symptom | Likely Cause |
|---------|--------------|
| "Cannot open device" | Device busy, permissions issue, or invalid path |
| 0 controls enumerated | Driver does not implement VIDIOC_QUERYCTRL, or camera has no configurable controls |
| ISX021 controls found but sweep captures fail | Control state puts sensor in an incompatible mode; STREAMON succeeds but no frames arrive |
| Some combinations return 0 frames | That specific control combination disables the sensor output or causes a firmware hang |
