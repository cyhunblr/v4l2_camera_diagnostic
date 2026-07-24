# t02 — V4L2 Control Inventory

**Layer:** 1 — Discovery  
**Category:** discovery  
**Trigger required:** no  

## Purpose

Enumerates all V4L2 controls exposed by the camera driver, reporting their ranges, step sizes, default values, and current values. Provides a complete inventory of adjustable parameters, which is essential for understanding what the driver exposes before running control-dependent tests.

## How It Works

1. Opens the device in non-blocking mode.
2. Iterates through all controls using `VIDIOC_QUERYCTRL` with `V4L2_CTRL_FLAG_NEXT_CTRL`.
3. Skips disabled controls.
4. For each active control, reads the current value via `VIDIOC_G_CTRL`.
5. Records the control name, hex ID, min/max/step/default/current values, and read-only status.
6. Counts total controls and writable controls as summary metrics.

## Implementation

Function: `run_control_inventory` in [diagnostic_runner.cpp](../../../source/backend/core/src/diagnostic_runner.cpp)  
Registry: `t02-control-inventory` in [test_registry.cpp](../../../source/backend/core/src/test_registry.cpp)

> The source file contains `// Docs: docs/backend/tests/t02-control-inventory.md`
> above the function as a back-reference to this document.

## Parameters

No configurable parameters.

## Output Metrics

| Key | Unit | Description |
| --- | ---- | ----------- |
| `control_count` | count | Total number of non-disabled controls enumerated |
| `writable_count` | count | Number of controls that are not read-only |

## Report Details

Each control produces one detail line in this format:

```text
<name> [0x<hex_id>] min=<min> max=<max> step=<step> default=<default> current=<current> [RO]
```

Examples:

```text
Brightness [0x00980900] min=0 max=255 step=1 default=128 current=128
Exposure (Absolute) [0x009a0902] min=1 max=10000 step=1 default=166 current=166 [RO]
```

## Verdict Logic

| Status | Condition |
| ------ | --------- |
| **Pass** | Device was opened and controls were enumerated successfully (always passes if device opens) |
| **Fail** | Cannot open device (permission error or invalid path) |

## Interpretation Guide

- A high `control_count` with many writable controls indicates a full-featured driver.
- Vendor-specific controls (IDs in `0x009aXXXX` range) indicate ISX021 or other sensor-specific extensions.
- Controls marked `[RO]` cannot be adjusted by the user — they are informational or driver-managed.
- `writable_count = 0` means the driver only exposes read-only status controls.

## Failure Modes

| Symptom | Likely Cause |
| --------- | -------------- |
| "Cannot open device" | Device node missing or insufficient permissions |
| `control_count = 0` | Driver does not implement the control framework (very old or minimal driver) |
| Missing expected ISX021 controls | Wrong device node, or sensor not fully initialized by firmware |
