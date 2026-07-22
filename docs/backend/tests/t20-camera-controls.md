# t20 — Camera control parameter effect

> - **Implementation:** search `run_t20` in [diagnostic_runner.cpp](../../../source/backend/core/src/diagnostic_runner.cpp)
> - **Definition:** search `t20-camera-controls` in [test_registry.cpp](../../../source/backend/core/src/test_registry.cpp)
> - **Category:** `controls`
> - **`uses_trigger`:** yes (only in the ISX021 sweep phase — see §2)
> - **Trigger modes:** all (Hardware, Software, FreeRun)
> - **Experimental, risky, profile-scoped:** yes — the sweep phase **writes** ISX021-specific sensor registers

## 1. Overview / scope

**What it does:** enumerates every V4L2 control the driver exposes, then —
**only if the sensor is an ISX021** (detected by the presence of three
ISX021-specific control IDs) — sweeps all 8 combinations of three
ISX021-specific ISP controls and measures capture latency at each
combination.

**Questions it answers:**

- What V4L2 controls does this device expose, and which are writable?
- Is this an ISX021 sensor (i.e. does it expose the three
  ISX021-specific control IDs this test looks for)?
- If it is, does capture latency change depending on the combination of
  those three ISX021-specific settings?

**Why it matters:** this is the only test in the suite that actively
**writes device state** as part of its measurement — it is the direct
counterpart to [t24](t24-control-inventory.md), which performs the same
enumeration but never writes anything.

**Method:** enumerate all controls via the standard `NEXT_CTRL` walk; if the
three ISX021 control IDs are present, save their current values, then for
each of the 8 combinations of `{0,1}³`, write the combination, run a fresh
capture session, measure latency over 20 samples, and record the per-combo
mean — finally restoring the original values.

## 2. Trigger modes

No restriction on the run's selected trigger mode — but note that a trigger
is only actually **used** in the ISX021 sweep phase (§1), which only runs
when the sensor is detected as ISX021. The inventory-only phase for
non-ISX021 sensors fires no trigger at all.

## 3. Inputs

All parameters are fixed in `run_t20`:

| Input | Value | Meaning |
| --- | --- | --- |
| ISX021 detection IDs | `0x9a206d` (LL), `0x9a2064` (BP), `0x9a2068` (WI) | Presence of **any** of these three control IDs marks the sensor as ISX021. |
| Sweep combinations | `2 × 2 × 2 = 8` | Every combination of `{0,1}` for LL, BP, WI. |
| Per-combo warm-up | `8` triggers @ `200 ms` | Before each combination's capture loop. |
| Per-combo samples | `20` | Capture attempts, `200 ms` timeout each. |
| Buffer count | `2` | Per sweep-combo session. |

> **Preconditions:** for the inventory phase, any openable V4L2 device. For
> the sweep phase to run at all, the device must additionally expose the
> three ISX021 control IDs above, and a working trigger for the selected
> mode (sweep captures use it; if it is unavailable, that combination's
> captures simply yield no data, but the sweep still writes and restores the
> controls).

## 4. Outputs & interpretation

### Metrics

| Metric | Unit | Meaning |
| --- | --- | --- |
| `control_count` | count | Total non-disabled controls enumerated. |
| `writable_count` | count | Of those, how many are **not** read-only. |
| `isx021_found` | bool | Whether any of the three ISX021 control IDs were present. |
| `ll<0/1>_bp<0/1>_wi<0/1>_mean_ms` | ms | Mean capture latency for that specific combination (present only if ≥1 sample succeeded for it) — e.g. `ll0_bp1_wi0_mean_ms`. |

`details` gets one line per enumerated control (name, hex ID, min/max/step/
default/current value, `[RO]` if read-only), plus — for ISX021 sensors —
one line per sweep combination with its sample count and mean latency.

### Status

| Status | Condition | Meaning |
| --- | --- | --- |
| `Pass` | device opened successfully | Always, whether or not ISX021 controls were found or the sweep ran. |
| `Error` | the device could not be opened | Setup problem, not a result (see summary). |

There is no `Fail`/`Warn` — like [t06](t06-format-comparison.md) and
[t24](t24-control-inventory.md), this test characterizes rather than
asserts.

### How to read the numbers

- **`isx021_found == false`** — the summary will say so directly ("ISX021
  specific controls unavailable") and no sweep runs; only the inventory
  metrics are meaningful for this run.
- **Comparing the 8 `*_mean_ms` metrics** — if they cluster tightly
  together, these three ISX021 settings don't meaningfully affect capture
  latency on this hardware; a clear outlier combination is worth
  investigating further (and cross-referencing with image-quality
  observations, since these are ISP settings, not just timing knobs).
- **Missing combo metrics** — if a particular `llX_bpY_wiZ_mean_ms` is
  absent, that combination's session failed to start or captured zero
  frames; check `details` for that combo's line (or its absence).
- **Original values are always restored** — even if the sweep is
  interrupted partway, the code path always calls `set_c()` for LL/BP/WI
  back to their original values at the end of the sweep, so re-running
  other tests afterward should see the sensor in its original state.

## 5. How the code works

`run_t20`:

1. **Open control fd** — opens the device directly (`O_RDWR | O_NONBLOCK`).
   Failure yields `Error`.
2. **Enumerate controls** — walks `VIDIOC_QUERYCTRL` with
   `V4L2_CTRL_FLAG_NEXT_CTRL`, skipping disabled controls, counting
   writable ones, reading each control's current value via
   `VIDIOC_G_CTRL`, and recording a details line per control. Along the way,
   checks whether any control ID matches one of the three ISX021 IDs.
3. **Non-ISX021 exit** — if no ISX021 controls were found, closes the fd,
   reports `Pass` with the inventory summary, and returns — **the sweep
   phase does not run**.
4. **ISX021 sweep** (only reached if ISX021 controls were found): reads and
   saves the three controls' original values via local `get_c`/`set_c`
   lambdas wrapping `VIDIOC_G_CTRL`/`VIDIOC_S_CTRL`. Then, for each of the 8
   `{0,1}³` combinations: writes all three controls, opens a **fresh**
   `V4lSession`, starts streaming, warms up with 8 triggers, captures 20
   samples, and — if any succeeded — pushes that combination's mean latency
   metric and details line.
5. **Restore + verdict** — writes the three controls back to their saved
   original values, closes the fd, and reports `Pass` with the sweep
   summary.

Unlike [t24](t24-control-inventory.md)'s identical-looking enumeration
logic, `run_t20` uses `def=`/`cur=` labels in its details lines (t24 uses
`default=`/`current=`) — a minor cosmetic difference between the two
otherwise-parallel inventory implementations.
