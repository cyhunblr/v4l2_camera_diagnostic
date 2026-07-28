# t17 — Format Comparison

**Layer:** 5 — Latency  
**Category:** format  
**Trigger modes:** Hardware | Software | FreeRun  
**Tags:** `benchmark`  

## Purpose

Compares capture performance across all pixel formats the device advertises via `VIDIOC_ENUM_FMT`. For each supported single-planar format, the test measures trigger-to-DQBUF latency and memcpy throughput. This identifies whether format choice affects pipeline latency or memory transfer speed, helping select the optimal format for the application's requirements.

## How It Works

1. Opens the device, saves the current format, and enumerates all single-planar capture formats via `VIDIOC_ENUM_FMT`.
2. Closes the enumeration file descriptor to avoid resource contention.
3. For each enumerated format:
   a. Opens a fresh V4L2 session and sets the pixel format at the device's current resolution using `VIDIOC_S_FMT`.
   b. Verifies the granted `pixelformat` matches the request; `VIDIOC_S_FMT` may substitute a different format and still succeed, so mismatches are skipped rather than measured.
   c. Starts streaming with 2 buffers, warms up, and captures `sample_count` frames recording latency for each.
   d. On the first successful capture, benchmarks memcpy throughput by copying `sizeimage` bytes `throughput_reps` times.
4. Restores the original device format and closes.

## Implementation

Function: `run_format_comparison` in [diagnostic_runner.cpp](../../../source/backend/core/src/diagnostic_runner.cpp)  
Registry: `t17-format-comparison` in [test_registry.cpp](../../../source/backend/core/src/test_registry.cpp)

> The source file contains `// Docs: docs/backend/tests/t17-format-comparison.md`
> above the function as a back-reference to this document.

## Parameters

| Key | Default | Unit | Description |
| ----- | --------- | ------ | ------------- |
| `sample_count` | 20 | count | Captures per format |
| `throughput_reps` | 50 | count | Memcpy repetitions for throughput benchmark |
| `capture_timeout_ms` | 500 | ms | Safety timeout per capture attempt (prevents hang) |
| `warmup_count` | 5 | count | Warmup frames before measurement begins |

## Output Metrics

### Summary metrics

| Metric Key | Unit | Description |
| ---------- | ---- | ----------- |
| `format_count` | count | Number of formats enumerated from the device |
| `formats_tested` | count | Number of formats that successfully captured frames |

### Per-format latency metrics (prefix = lowercase fourcc, e.g. `yuyv`, `rggb`)

| Metric Key Pattern | Unit | Description |
| ------------------- | ------ | ------------- |
| `{fmt}_latency_mean` | ms | Mean trigger-to-DQBUF latency |
| `{fmt}_latency_max` | ms | Maximum latency |

### Per-format throughput metric

| Metric Key Pattern | Unit | Description |
| ------------------ | ---- | ----------- |
| `{fmt}_throughput_mbps` | MB/s | Memcpy throughput for the format's sizeimage |

## Report Details

```text
RGGB: sizeimage=2073600
RG16: sizeimage=4147200
```

If a format fails to set or start:

```text
UYVY: S_FMT failed
NV12: start failed: VIDIOC_STREAMON: Device or resource busy
YUYV: driver substituted UYVY — skipped
```

## Verdict Logic

| Status | Condition |
| ------ | --------- |
| **Pass** | At least one format successfully captured frames |
| **Warn** | No format could be tested (all sessions failed) |
| **Fail** | Cannot open the device at all |

## Interpretation Guide

- **All formats have similar latency**: Format choice does not affect pipeline latency — pick based on downstream processing needs.
- **One format has significantly lower latency**: The camera's ISP or readout path favors that format — prefer it for latency-sensitive applications.
- **Throughput differs between formats**: May indicate different sizeimage (padding) or memory layout efficiency.
- **`formats_tested < format_count`**: Some formats enumerated but could not be captured — partial driver support or format incompatibility at the current resolution.
- **`formats_tested = 0`**: No format could stream — check device availability and driver state.

## Failure Modes

| Symptom | Likely Cause |
| --------- | -------------- |
| "Cannot open device" | Device busy, permissions issue, or invalid device path |
| "S_FMT failed" for all formats | Camera does not support any enumerated format at the current resolution |
| "start failed" | Buffer allocation or STREAMON fails for this format |
| Throughput = 0 | First capture failed or sizeimage reported as 0 by driver |
| `format_count = 0` | Device does not enumerate any single-planar capture formats |
