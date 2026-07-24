# t16 — Format Comparison

**Layer:** 5 — Latency  
**Category:** format  
**Trigger required:** yes  

## Purpose

Compares capture performance between supported pixel formats (YUYV and UYVY) by measuring both trigger-to-DQBUF latency and memcpy throughput for each format. This identifies whether format choice affects pipeline latency or memory transfer speed, helping select the optimal format for the application's latency and bandwidth requirements.

## How It Works

1. Opens the device to save the original format and iterates over the two test formats: YUYV and UYVY.
2. For each format:
   a. Sets the pixel format to the target fourcc at the configured resolution (default 1920×1280) using `VIDIOC_S_FMT`.
   b. Opens a fresh V4L2 session, applies the format, starts streaming, and warms up.
   c. Captures `sample_count` frames, recording latency for each successful capture.
   d. On the first successful capture, benchmarks memcpy throughput by copying `sizeimage` bytes `throughput_reps` times.
   e. Computes full latency statistics and reports throughput in MB/s.
3. Restores the original device format and closes.

## Implementation

Function: `run_format_comparison` in [diagnostic_runner.cpp](../../../source/backend/core/src/diagnostic_runner.cpp)  
Registry: `t16-format-comparison` in [test_registry.cpp](../../../source/backend/core/src/test_registry.cpp)

> The source file contains `// Docs: docs/backend/tests/t16-format-comparison.md`
> above the function as a back-reference to this document.

## Parameters

| Key | Default | Unit | Description |
| ----- | --------- | ------ | ------------- |
| `sample_count` | 20 | count | Captures per format |
| `throughput_reps` | 50 | count | Memcpy repetitions for throughput benchmark |
| `width` | 1920 | px | Capture width for format test |
| `height` | 1280 | px | Capture height for format test |

## Output Metrics

### Per-format latency metrics (prefix = lowercase format name: `yuyv`, `uyvy`)

| Metric Key Pattern | Unit | Description |
| ------------------- | ------ | ------------- |
| `{fmt}_latency_mean` | ms | Mean trigger-to-DQBUF latency |
| `{fmt}_latency_stddev` | ms | Standard deviation |
| `{fmt}_latency_min` | ms | Minimum latency |
| `{fmt}_latency_max` | ms | Maximum latency |
| `{fmt}_latency_p95` | ms | 95th percentile latency |
| `{fmt}_latency_jitter` | ms | Jitter (max - min) |

### Per-format throughput metric

| Metric Key Pattern | Unit | Description |
| ------------------ | ---- | ----------- |
| `{fmt}_throughput_mbps` | MB/s | Memcpy throughput for the format's sizeimage |

## Report Details

```text
YUYV: sizeimage=4915200
UYVY: sizeimage=4915200
```

If a format fails to set:

```text
UYVY: S_FMT failed
```

If a format fails to start:

```text
UYVY: start failed: VIDIOC_STREAMON: Device or resource busy
```

## Verdict Logic

| Status | Condition |
| ------ | --------- |
| **Pass** | Test completes (even if one or both formats fail to capture — metrics will be absent) |
| **Fail** | Cannot open the device at all |

This test is informational — it does not apply pass/fail thresholds on latency or throughput differences.

## Interpretation Guide

- **Both formats have similar latency**: Format choice does not affect pipeline latency — pick based on downstream processing needs.
- **One format has significantly lower latency**: The camera's ISP or readout path favors that format — prefer it for latency-sensitive applications.
- **Throughput differs between formats**: May indicate different sizeimage (padding) or memory layout efficiency.
- **S_FMT failed for a format**: Camera does not support that pixel format at the requested resolution.
- **One format captures 0 frames**: Camera can set the format but cannot stream it — partial driver support.

## Failure Modes

| Symptom | Likely Cause |
| --------- | -------------- |
| "Cannot open device" | Device busy, permissions issue, or invalid device path |
| "S_FMT failed" for both formats | Camera does not support YUYV or UYVY — different sensor format (e.g., SRGGB) |
| "start failed" | Buffer allocation fails at the requested resolution for this format |
| Throughput = 0 | First capture failed or sizeimage reported as 0 by driver |
| Only one format has metrics | Other format is not supported by the camera hardware |
