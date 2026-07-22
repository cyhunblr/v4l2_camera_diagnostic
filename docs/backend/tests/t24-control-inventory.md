# t24 — V4L2 control inventory

> - **Implementation:** search `run_t24` in [diagnostic_runner.cpp](../../../source/backend/core/src/diagnostic_runner.cpp)
> - **Definition:** search `t24-control-inventory` in [test_registry.cpp](../../../source/backend/core/src/test_registry.cpp)
> - **Category:** `controls`
> - **`uses_trigger`:** no
> - **Trigger modes:** all — irrelevant, no capture or streaming is involved
> - No hardware trigger required. Not in the original legacy diagnostic — added when the modular runner was built.

## 1. Overview / scope

**What it does:** enumerates every V4L2 control the driver exposes and
reports each one's name, ID, range, step, default, and current value —
without ever writing to the device.

**Questions it answers:**

- What controls does this device expose, and what are their current values?
- How many of those controls are writable versus read-only?

**Why it matters:** this is the suite's **read-only** control inventory —
useful whenever you need a full picture of a device's tunable parameters
without touching device state at all. It is the direct counterpart to
[t20](t20-camera-controls.md), which performs the same enumeration but then
actively writes ISX021-specific registers when that sensor is detected.

**Method:** open the device, walk every control via the standard
`NEXT_CTRL` query loop, and record its metadata and current value.

## 2. Trigger modes

Not applicable — this test never fires a trigger or starts streaming. It
runs identically regardless of the selected trigger mode.

## 3. Inputs

This test takes no configurable parameters — it enumerates whatever
controls the driver reports.

> **Preconditions:** an openable V4L2 device node. No streaming, trigger, or
> GPIO/control channel is required.

## 4. Outputs & interpretation

### Metrics

| Metric | Unit | Meaning |
| --- | --- | --- |
| `control_count` | count | Total non-disabled controls enumerated. |
| `writable_count` | count | Of those, how many are **not** read-only. |

`details` gets one line per control: name, hex ID, min/max/step/default/
current value, and `[RO]` if read-only.

### Status

| Status | Condition | Meaning |
| --- | --- | --- |
| `Pass` | device opened successfully | Always — this is a pure enumeration, not a check against an expected control set. |
| `Error` | the device could not be opened | Setup problem, not a result (see summary). |

### How to read the numbers

- **`control_count`/`writable_count` on their own aren't "good" or "bad"** —
  compare against your device's documented control set to confirm nothing
  unexpected is missing, or use this as a quick reference when writing
  automation against specific control IDs.
- **`details`** is the actual payload for this test — it's where the
  per-control ranges and current values live; the metrics are just
  aggregate counts.
- **Compare against [t20](t20-camera-controls.md)'s inventory** if you also
  ran that test — the enumeration logic is nearly identical (see §5), so
  the control counts should match; a difference would indicate the two
  tests ran against different device states or something changed between
  runs.

## 5. How the code works

`run_t24`:

1. **Open control fd** — opens the device directly (`O_RDWR | O_NONBLOCK`).
   Failure yields `Error`.
2. **Enumerate controls** — walks `VIDIOC_QUERYCTRL` with
   `V4L2_CTRL_FLAG_NEXT_CTRL`, skipping disabled controls, counting
   writable ones, reading each control's current value via
   `VIDIOC_G_CTRL`, and recording a details line per control.
3. **Aggregate + verdict** — closes the fd, pushes `control_count` and
   `writable_count`, and always reports `Pass`.

This enumeration logic is structurally identical to
[t20](t20-camera-controls.md)'s inventory phase, with two small
differences: `run_t24` labels its details lines `default=`/`current=` (t20
uses the shorter `def=`/`cur=`), and — critically — `run_t24` **never
writes** to any control, whereas t20 goes on to sweep and write ISX021
registers when that sensor is detected. If you need a guaranteed read-only
control snapshot, use this test rather than t20.

This test is one of three added when the modular runner was built that
never existed in the original legacy diagnostic (alongside
[t21](t21-stuck-frame.md) and [t22](t22-latency-under-load.md)) — see
[docs/testing.md](../../testing.md) for the full migration inventory.
