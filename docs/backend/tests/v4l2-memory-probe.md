# v4l2-memory-probe — V4L2 memory backend probe

> - **Implementation:** search `v4l2-memory-probe` in [diagnostic_runner.cpp](../../../source/backend/core/src/diagnostic_runner.cpp) — this test is an **inline dispatch block**, not a separate `run_tXX` function.
> - **Definition:** search `v4l2-memory-probe` in [test_registry.cpp](../../../source/backend/core/src/test_registry.cpp)
> - **Category:** `v4l2`
> - **`uses_trigger`:** no
> - **Trigger modes:** all — irrelevant, no capture or streaming is involved
> - **Always modular** — unlike every numbered test, this one has no legacy-diagnostic counterpart at all; it exists only in the modular runner.

## 1. Overview / scope

**What it checks:** which of the three memory backends — `mmap`, `dmabuf`,
`userptr` — `VIDIOC_REQBUFS` actually accepts on this device, and whether
the backend selected for the current run is among them.

**Questions it answers:**

- Which memory backends does this device's driver support at all?
- Is the memory backend chosen for this run actually one the device
  accepts?

**Why it matters:** every capture-based test in this suite depends on the
selected memory backend being valid for the device. This is the dedicated,
standalone check for that — a quick way to confirm backend compatibility
before (or independently of) running the rest of the suite, and the natural
counterpart to [t16](t16-v4l2-compliance.md)'s general capture/streaming
capability check.

**Method:** attempt `VIDIOC_REQBUFS` for each of the three memory backends
in turn and record whether each was accepted.

## 2. Trigger modes

Not applicable — this test never fires a trigger or captures a frame. It
runs identically regardless of the selected trigger mode.

## 3. Inputs

This test takes no configurable parameters — it probes all three backends
unconditionally.

| Input | Value | Meaning |
| --- | --- | --- |
| Backends probed | `mmap`, `dmabuf`, `userptr` | All three, regardless of which one is selected for the run. |
| Selected backend | the run's chosen backend | Only used to decide `Pass` vs. `Warn` — not to limit which backends are probed. |

> **Preconditions:** an openable V4L2 device node.

## 4. Outputs & interpretation

### Metrics

| Metric | Unit | Meaning |
| --- | --- | --- |
| `backend_mmap` | bool | Whether `VIDIOC_REQBUFS` accepted the `mmap` backend. |
| `backend_dmabuf` | bool | Whether `VIDIOC_REQBUFS` accepted the `dmabuf` backend. |
| `backend_userptr` | bool | Whether `VIDIOC_REQBUFS` accepted the `userptr` backend. |

`details` gets one line per backend: supported/unsupported, plus a
detail string from the underlying probe.

### Status

| Status | Condition | Meaning |
| --- | --- | --- |
| `Pass` | the run's **selected** backend was found supported | The backend actually in use for this run is valid on this device. |
| `Warn` | the selected backend was **not** found supported | The run is using a backend this device's `VIDIOC_REQBUFS` did not accept — every capture-based test in the same run is suspect. |

There is no `Fail`/`Error` — this test always completes (it does not open a
streaming session), so the worst outcome is `Warn`.

### How to read the numbers

- **A `Warn` here explains failures elsewhere in the same run** — if
  capture-based tests are erroring out, check this test's result first; a
  rejected backend means every test using it is running against a
  configuration the driver does not actually support.
- **Backends other than the selected one being unsupported is not
  inherently a problem** — most devices do not support all three; what
  matters is whether the **selected** one works, which is exactly what the
  `Pass`/`Warn` verdict reflects.
- **Compare against [t16](t16-v4l2-compliance.md)** — that test checks
  general capture/streaming capability; this one checks memory-backend
  compatibility specifically. A device can pass one and fail the other
  independently.

## 5. How the code works

Like [t16](t16-v4l2-compliance.md), `v4l2-memory-probe` has **no `run_tXX`
function** — it is handled entirely inline inside the `run_test()`
dispatch's `if`/`else if` chain, as the **last** named case before the
fallback:

1. **Probe all backends** — calls `probe_memory_backends(camera_path)`,
   which attempts `VIDIOC_REQBUFS` for each of `mmap`/`dmabuf`/`userptr` and
   returns a `{backend, supported, detail}` result for each.
2. **Report + check selection** — for every probed backend, pushes a
   `backend_<name>` metric and a details line; tracks whether the
   **currently selected** run backend was among the supported ones.
3. **Verdict** — `Pass` if the selected backend was supported, `Warn`
   otherwise.

### The fallback case

Immediately after this `else if` chain's last case (this test) comes the
dispatch's final `else` branch — reached only for an unrecognized test ID,
never for any of the 24 built-in tests documented in this directory:

```cpp
} else {
  result.status = TestStatus::Skipped;
  result.summary = "No core implementation registered for this test.";
}
```

See [docs/testing.md](../../testing.md) for why this fallback exists and
why it never fires for a real test in the current registry.
