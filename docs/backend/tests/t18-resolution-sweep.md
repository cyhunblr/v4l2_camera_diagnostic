# t18 — Resolution Sweep

**Layer:** 5 — Latency  
**Category:** format  
**Trigger required:** yes  

## Purpose

Enumerates all supported frame sizes (resolutions) for the current pixel format and measures capture latency and memory throughput at each resolution. This reveals how resolution affects pipeline latency and memory bandwidth requirements, helping select the optimal resolution for the application's performance constraints and identifying resolutions where the system becomes memory- or latency-bound.

## How It Works

1. Opens the device and saves the original format.
2. Enumerates frame sizes using `VIDIOC_ENUM_FRAMESIZES` for the current pixel format:
   - For **discrete** frame sizes: adds each reported resolution.
   - For **stepwise/continuous** ranges: samples min, mid, and max resolutions.
3. For each enumerated resolution:
   a. Sets the format to the target resolution via `VIDIOC_S_FMT`.
   b. Opens a fresh session, starts streaming with 2 buffers, and warms up (3 captures).
   c. Captures `sample_count` frames, recording latency for each successful capture.
   d. Benchmarks memcpy throughput on the first buffer by copying `sizeimage` bytes `throughput_reps` times.
   e. Computes latency statistics (mean, p95) and throughput in MB/s.
4. Restores the original device format and closes.

## Implementation

Function: `run_resolution_sweep` in [diagnostic_runner.cpp](../../../source/backend/core/src/diagnostic_runner.cpp)  
Registry: `t18-resolution-sweep` in [test_registry.cpp](../../../source/backend/core/src/test_registry.cpp)

> The source file contains `// Docs: docs/backend/tests/t18-resolution-sweep.md`
> above the function as a back-reference to this document.

## Parameters

| Key | Default | Unit | Description |
|-----|---------|------|-------------|
| `sample_count` | 15 | count | Captures per resolution |
| `throughput_reps` | 30 | count | Memcpy repetitions for throughput benchmark |

## Output Metrics

### Summary metric

| Metric Key | Unit | Description |
|-----------|------|-------------|
| `resolution_count` | count | Total number of resolutions enumerated |

### Per-resolution metrics (for each resolution WxH)

| Metric Key Pattern | Unit | Description |
|-------------------|------|-------------|
| `{W}x{H}_latency_mean` | ms | Mean trigger-to-DQBUF latency at this resolution |
| `{W}x{H}_latency_p95` | ms | 95th percentile latency at this resolution |
| `{W}x{H}_throughput_mbps` | MB/s | Memcpy throughput at this resolution |

Example metric keys: `1920x1280_latency_mean`, `1920x1280_latency_p95`, `1920x1280_throughput_mbps`, `640x480_latency_mean`, etc.

## Report Details

```
1920x1280: mean=34ms p95=37ms throughput=4521MB/s
1280x720: mean=22ms p95=24ms throughput=5102MB/s
640x480: mean=12ms p95=14ms throughput=7845MB/s
```

If a resolution fails:
```
3840x2160: S_FMT failed — skipped
1280x720: session failed — VIDIOC_REQBUFS: Cannot allocate memory
```

If no frames captured at a resolution:
```
1920x1080: no frames captured throughput=3200MB/s
```

## Verdict Logic

| Status | Condition |
|--------|-----------|
| **Pass** | At least one resolution was successfully tested |
| **Warn** | Device enumerates frame sizes but all S_FMT or session starts failed |
| **Warn** | Device does not enumerate any frame sizes for the current pixel format |
| **Fail** | Cannot open the device |

This test does not apply thresholds on latency or throughput — it is a characterization test.

## Interpretation Guide

- **Latency scales linearly with resolution**: Pipeline is readout-bound — higher resolution means more data to transfer from the sensor.
- **Latency roughly constant across resolutions**: Pipeline latency is dominated by fixed overhead (trigger processing, ISP startup) rather than data volume.
- **Throughput decreases at higher resolutions**: Larger frames exceed cache capacity — memory bandwidth becomes the bottleneck.
- **Some resolutions have 0 captures**: Camera or driver cannot stream at that resolution — partial hardware support.
- **resolution_count = 0**: Driver does not implement `VIDIOC_ENUM_FRAMESIZES` — format negotiation must be done manually.

## Failure Modes

| Symptom | Likely Cause |
|---------|--------------|
| "Cannot open device" | Device busy, permissions, or invalid path |
| "Device does not enumerate any frame sizes" | Driver does not implement VIDIOC_ENUM_FRAMESIZES for this pixel format |
| "S_FMT failed" for most resolutions | Driver only supports specific fixed resolutions not in the enumerated list |
| "session failed" at high resolutions | Insufficient contiguous memory for large frame buffers |
| No frames captured but session starts | Trigger not producing frames at the new resolution's timing requirements |
| Very low throughput at all resolutions | Uncached memory mapping or system memory bandwidth saturation |
