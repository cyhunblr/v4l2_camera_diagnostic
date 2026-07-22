# t06 — Format comparison (YUYV vs UYVY)

> - **Implementation:** search `run_t06` in [diagnostic_runner.cpp](../../../source/backend/core/src/diagnostic_runner.cpp)
> - **Definition:** search `t06-format-comparison` in [test_registry.cpp](../../../source/backend/core/src/test_registry.cpp)
> - **Category:** `format`
> - **`uses_trigger`:** yes
> - **Trigger modes:** all (Hardware, Software, FreeRun)

## 1. Overview / scope

**What it compares:** capture latency and memcpy throughput between two
pixel formats — **YUYV** and **UYVY** — at the same resolution.

**Questions it answers:**

- Does switching pixel format change trigger→frame latency?
- Does one format's buffer layout copy faster than the other?

**Why it matters:** YUYV and UYVY carry the same information in a different
byte order; if the driver or sensor handles one more efficiently than the
other, this test surfaces it directly instead of leaving format choice to
assumption.

**Method:** for each format, switch the device's capture format, start a
session, capture 20 triggered samples, and — on the very first successful
capture — benchmark a repeated memcpy of that frame's buffer to estimate
throughput.

## 2. Trigger modes

No restriction — format comparison applies the same way regardless of
trigger source.

## 3. Inputs

All parameters are fixed in `run_t06`:

| Input | Value | Meaning |
| --- | --- | --- |
| Formats compared | `YUYV`, `UYVY` | Tried in this order. |
| Resolution | `1920 × 1280` | Fixed for both formats. |
| Field order | `V4L2_FIELD_NONE` | Progressive, no interlacing. |
| Samples per format | `20` | Triggered captures. |
| Buffer count | `2` | Per-format session. |
| Sample interval | `200 ms` | Between captures. |
| Throughput reps | `50` | `memcpy` repetitions on the first captured frame, per format. |

> **Preconditions:** a streaming-capable camera supporting both formats at
> the given resolution, plus a working trigger. The device's original format
> is saved before the test and restored afterward.

## 4. Outputs & interpretation

### Metrics

| Metric | Unit | Meaning |
| --- | --- | --- |
| `yuyv_latency_mean` / `_stddev` / `_min` / `_max` / `_p95` / `_jitter` | ms | Latency distribution while capturing YUYV. Present only if ≥1 frame was captured in that format. |
| `yuyv_throughput_mbps` | MB/s | `memcpy` throughput measured on the first YUYV frame. |
| `uyvy_latency_mean` / `_stddev` / `_min` / `_max` / `_p95` / `_jitter` | ms | Latency distribution while capturing UYVY. |
| `uyvy_throughput_mbps` | MB/s | `memcpy` throughput measured on the first UYVY frame. |

`details` records, per format: whether `VIDIOC_S_FMT` succeeded, the
negotiated `sizeimage`, and any session start failure.

### Status

| Status | Condition | Meaning |
| --- | --- | --- |
| `Pass` | control fd opened successfully | Always — this test compares formats, it does not assert one is "correct". |
| `Error` | the device could not be opened at all | Setup problem, not a comparison result. |

A format that fails `S_FMT` or session start is skipped (recorded in
`details`) without failing the whole test — the other format's results still
stand on their own.

### How to read the numbers

- **Latency: YUYV vs UYVY** — compare `yuyv_latency_mean`/`_p95` against
  `uyvy_latency_mean`/`_p95`. Since both are captured through the same
  trigger/capture path at the same resolution, a real difference points at
  format-dependent driver or sensor behavior rather than measurement noise.
- **Throughput: YUYV vs UYVY** — `_throughput_mbps` is a single-frame,
  50-repetition memcpy benchmark, not a full-session average — treat it as
  indicative, not a statistically robust distribution. A large gap between
  formats at the same `sizeimage` is worth investigating (e.g. cache
  behavior from byte-order differences).
- **Missing metrics for a format** — if `yuyv_*` or `uyvy_*` metrics are
  absent, check `details` for that format: the device likely does not
  support it at this resolution, or the session failed to start.

## 5. How the code works

`run_t06`:

1. **Open control fd** — opens the device directly (`O_RDWR | O_NONBLOCK`)
   to drive `VIDIOC_G_FMT`/`VIDIOC_S_FMT` independently of the capture
   session. Saves the original format so it can be restored at the end.
   Failure here yields `Error`.
2. **Per-format loop** (`YUYV` then `UYVY`):
   - Builds the FourCC from the format string and sets it via `S_FMT` at
     `1920×1280`, `V4L2_FIELD_NONE`. A failure here is recorded in `details`
     and the format is skipped.
   - Opens a fresh `V4lSession`, re-applies the same format on the session's
     own fd, starts streaming with `2` buffers, and warms up.
   - Captures 20 triggered samples. On the **first** successful capture
     only, reads the negotiated `sizeimage`, points directly at the mapped
     buffer for that frame index, and times 50 back-to-back `memcpy` calls
     to compute `_throughput_mbps`.
   - If any frames were captured, pushes the `compute_stats()`-derived
     `<format>_latency_*` family via `push_stats_metrics()` plus the
     throughput metric.
3. **Restore + verdict** — restores the original format on the control fd,
   closes it, and always reports `Pass` — this test characterizes both
   formats side by side rather than asserting either one is correct.

Note: the throughput benchmark reads from the **live mapped buffer**
(`s.buffers()[f.index].data()`) immediately after capture, before it is
requeued — this measures the same memory the application would actually
process, not a synthetic copy.
