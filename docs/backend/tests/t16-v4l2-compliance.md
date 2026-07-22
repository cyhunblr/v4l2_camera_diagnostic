# t16 — V4L2 compliance checks

> - **Implementation:** search `t16-v4l2-compliance` in [diagnostic_runner.cpp](../../../source/backend/core/src/diagnostic_runner.cpp) — this test is an **inline dispatch block**, not a separate `run_tXX` function.
> - **Definition:** search `t16-v4l2-compliance` in [test_registry.cpp](../../../source/backend/core/src/test_registry.cpp)
> - **Category:** `v4l2`
> - **`uses_trigger`:** no
> - **Trigger modes:** all — irrelevant, no capture or streaming is involved

## 1. Overview / scope

**What it checks:** whether the device reports the minimum V4L2 capability
set this suite depends on — capture support and streaming I/O support — plus
an inventory of the pixel formats it advertises.

**Questions it answers:**

- Does `VIDIOC_QUERYCAP` report `V4L2_CAP_VIDEO_CAPTURE`?
- Does it report `V4L2_CAP_STREAMING`?
- What pixel formats does the device advertise via format enumeration?

**Why it matters:** every other capture-based test in this suite **assumes**
the device supports capture and streaming. This test is the explicit,
standalone check for that assumption — a quick way to confirm a device is
even a valid target before running the rest of the suite against it.

**Method:** query the device's V4L2 capabilities and enumerate its supported
pixel formats; report driver/card/bus identification alongside the
capability flags.

## 2. Trigger modes

Not applicable — this test never fires a trigger, opens a streaming session,
or captures a frame. It runs identically regardless of the selected trigger
mode.

## 3. Inputs

This test takes no configurable parameters — it is a single capability
query.

> **Preconditions:** an openable V4L2 device node. No streaming, trigger, or
> GPIO/control channel is required.

## 4. Outputs & interpretation

### Metrics

| Metric | Unit | Meaning |
| --- | --- | --- |
| `supports_capture` | bool | Whether `VIDIOC_QUERYCAP` reports `V4L2_CAP_VIDEO_CAPTURE`. |
| `supports_streaming` | bool | Whether `VIDIOC_QUERYCAP` reports `V4L2_CAP_STREAMING`. |
| `format_count` | count | Number of pixel formats enumerated for the capture buffer type. |

`details` records the driver name, card name, bus info, the currently
selected memory backend, and one line per enumerated format (FourCC,
description, buffer type).

### Status

| Status | Condition | Meaning |
| --- | --- | --- |
| `Pass` | `supports_capture` **and** `supports_streaming` are both true | The device exposes the minimum capability set this suite requires. |
| `Fail` | either capability flag is false | The device does not support capture and/or streaming I/O — most other tests in this suite cannot run meaningfully against it. |
| `Error` | the device query itself failed | Could not even query the device (see summary for the underlying error). |

### How to read the numbers

- **A `Fail` here explains failures elsewhere** — if other tests in the same
  run are erroring or behaving unexpectedly, check this test's result
  first; a device lacking capture/streaming support will not behave
  correctly under any of the capture-based tests.
- **`format_count`** — a healthy capture device typically advertises more
  than one format; `0` formats alongside a `Pass` capability check would be
  unusual and worth investigating directly with `v4l2-ctl --list-formats`.
- **Driver/card/bus details** — useful to confirm you are testing the
  device you think you are, especially on systems with multiple V4L2 nodes.

## 5. How the code works

Unlike every other test described in this directory, `t16-v4l2-compliance`
has **no `run_tXX` function** — it is handled entirely inline inside the
`run_test()` dispatch's `if`/`else if` chain (alongside
[v4l2-memory-probe](v4l2-memory-probe.md), the suite's only other inline
test):

1. **Query** — calls `query_device(camera_path, &info)`, which wraps
   `VIDIOC_QUERYCAP` and format enumeration into a single `DeviceInfo`
   struct. A query failure yields `Error`.
2. **Verdict** — `Pass` if both `info.supports_capture` and
   `info.supports_streaming` are true, otherwise `Fail`.
3. **Report** — pushes the three metrics, then details for driver/card/bus/
   selected-backend, followed by one details line per enumerated format.

Because this is inline rather than a named function, there is no `// Docs:`
comment above a function definition for it — the doc-to-code backlink lives
as a comment directly above this `else if` block in `diagnostic_runner.cpp`.
