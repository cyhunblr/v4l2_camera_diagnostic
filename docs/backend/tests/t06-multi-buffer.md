# t06 — Multi-Buffer Configurations

**Layer:** 3 — Buffer & memory  
**Category:** buffering  
**Trigger required:** no  

## Purpose

Probes the driver's buffer allocation behavior by requesting 1 through 5 buffers via `VIDIOC_REQBUFS`, recording how many buffers the driver actually grants. When a trigger source is available, it also measures capture latency at each buffer count to determine the optimal configuration.

## How It Works

1. For each buffer count in {1, 2, 3, 4, 5}:
   a. Opens the device and issues `VIDIOC_REQBUFS` with the requested count.
   b. Records the granted count (the driver may grant fewer or more than requested).
   c. Releases the buffers by setting count to 0.
   d. If a GPIO trigger is available: starts streaming with the requested buffer count, warms up (3 frames), captures 20 samples measuring latency, then reports mean latency and miss count.
2. Reports granted counts and (optionally) per-count latency statistics.

## Implementation

Function: `run_multi_buffer` in [diagnostic_runner.cpp](../../../source/backend/core/src/diagnostic_runner.cpp)  
Registry: `t06-multi-buffer` in [test_registry.cpp](../../../source/backend/core/src/test_registry.cpp)

> The source file contains `// Docs: docs/backend/tests/t06-multi-buffer.md`
> above the function as a back-reference to this document.

## Parameters

| Key | Default | Unit | Description |
| ----- | --------- | ------ | ------------- |
| `sample_count` | 20 | count | Frames to capture per buffer count (when trigger available) |
| `max_buffers` | 5 | count | Maximum buffer count to test |
| `warmup_count` | 3 | count | Warmup frames per configuration |
| `capture_timeout_ms` | 100 | ms | Per-frame capture timeout |
| `sample_interval_ms` | 200 | ms | Delay between captures |

## Output Metrics

| Key | Unit | Description |
| ----- | ------ | ------------- |
| `granted_for_1` | count | Buffers granted when 1 requested |
| `granted_for_2` | count | Buffers granted when 2 requested |
| `granted_for_3` | count | Buffers granted when 3 requested |
| `granted_for_4` | count | Buffers granted when 4 requested |
| `granted_for_5` | count | Buffers granted when 5 requested |

## Report Details

With trigger available:

```text
count=1 granted=2 mean=34ms miss=0/20
count=2 granted=2 mean=33ms miss=0/20
count=3 granted=3 mean=33ms miss=0/20
count=4 granted=4 mean=34ms miss=1/20
count=5 granted=5 mean=33ms miss=0/20
```

Without trigger:

```text
count=1 granted=2 (GPIO unavailable, capture skipped)
count=2 granted=2 (GPIO unavailable, capture skipped)
```

## Verdict Logic

| Status | Condition |
| ------ | --------- |
| **Pass** | Always passes (informational test) — the probe completes regardless of results |

## Interpretation Guide

- `granted_for_N > N`: Driver allocates a minimum number of buffers (e.g., always grants at least 2).
- `granted_for_N < N`: Driver memory pressure — cannot allocate the requested count.
- `granted_for_N = 0`: Critical — driver refuses to allocate any buffers at this count.
- Lower latency at higher buffer counts: More buffers reduce the chance of queue starvation.
- Higher miss rate at count=1: Single-buffer operation is unreliable for triggered capture.
- Consistent latency across all counts: The pipeline's latency is dominated by the sensor/trigger path, not buffer depth.

## Failure Modes

| Symptom | Likely Cause |
| --------- | -------------- |
| "open failed" for a buffer count | Device node permissions or driver error |
| "REQBUFS failed" | Driver cannot allocate any buffers (memory exhaustion or incompatible type) |
| "start failed" | Driver refuses to stream with the requested buffer count |
| All granted counts = minimum (e.g., 2) | Driver enforces a fixed buffer pool size |
