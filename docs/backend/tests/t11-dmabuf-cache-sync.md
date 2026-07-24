# t11 — DMA_BUF_IOCTL_SYNC Cache Coherency

**Layer:** 3 — Buffer & memory  
**Category:** dmabuf  
**Trigger required:** yes  

## Purpose

Determines whether `DMA_BUF_IOCTL_SYNC` is required for cache-coherent reads on this platform. Some SoCs have cache-coherent DMA paths where the sync is unnecessary, while others require explicit cache invalidation before reading DMA buffer contents from the CPU. This test compares mmap and dmabuf buffer content with and without the sync ioctl to detect coherency behavior, informing whether production code must bracket DMA reads with sync calls.

## How It Works

1. Opens the camera device and starts streaming with the Dmabuf memory backend (forced regardless of global setting).
2. Warms up the pipeline with several trigger/capture cycles.
3. For each of `sample_count` frames:
   a. Captures a frame without auto-requeue (retains the buffer).
   b. Reads the first `compare_bytes` from the mmap pointer.
   c. Reads the first `compare_bytes` from the DMA pointer **without** sync → compares to mmap data.
   d. Issues `DMA_BUF_IOCTL_SYNC` (START + READ), reads `compare_bytes` from DMA pointer, issues sync END → compares to mmap data.
   e. Requeues the buffer.
4. Counts match ratios for both with-sync and without-sync paths.

## Implementation

Function: `run_dmabuf_cache_sync` in [diagnostic_runner.cpp](../../../source/backend/core/src/diagnostic_runner.cpp)  
Registry: `t11-dmabuf-cache-sync` in [test_registry.cpp](../../../source/backend/core/src/test_registry.cpp)

> The source file contains `// Docs: docs/backend/tests/t11-dmabuf-cache-sync.md`
> above the function as a back-reference to this document.

## Parameters

| Key | Default | Unit | Description |
| ----- | --------- | ------ | ------------- |
| `sample_count` | 20 | frames | Number of frames to compare |
| `compare_bytes` | 64 | bytes | Number of leading bytes compared per frame |
| `capture_timeout_ms` | 100 | ms | Poll timeout for each capture attempt |

## Output Metrics

| Metric Key | Unit | Description |
| ----------- | ------ | ------------- |
| `frames_tested` | count | Number of frames successfully compared |
| `match_without_sync` | count | Frames where DMA read matched mmap without sync |
| `match_with_sync` | count | Frames where DMA read matched mmap after sync |
| `sync_required` | bool | 1.0 if sync is needed (without-sync mismatches but with-sync matches all), 0.0 otherwise |

## Report Details

This test does not emit explicit detail lines beyond the metrics. The summary message indicates the coherency conclusion.

## Verdict Logic

| Status | Condition |
| -------- | ----------- |
| **Pass** | `match_with_sync / frames_tested >= min_match_ratio` (default: 1.0 = all frames must match) |
| **Fail** | Match ratio with sync is below threshold — cache coherency failure |
| **Fail** | No frames were compared (`frames_tested == 0`) |
| **Skipped** | `linux/dma-buf.h` header not available at compile time |

**Threshold:** `min_match_ratio` = 1.0 (from `default_threshold_config`)

## Interpretation Guide

- **sync_required = 1.0**: This platform requires `DMA_BUF_IOCTL_SYNC` before CPU reads from DMA buffers. Production code must bracket reads with sync start/end.
- **sync_required = 0.0, all match_without_sync == frames_tested**: Platform has hardware cache coherency — sync calls are unnecessary overhead.
- **match_with_sync < frames_tested**: Serious coherency issue — even with explicit sync, DMA and mmap views disagree. May indicate a driver bug or memory-map aliasing problem.
- **frames_tested much less than sample_count**: Many captures failed or returned insufficient data — check trigger and capture pipeline health.

## Failure Modes

| Symptom | Likely Cause |
| --------- | -------------- |
| "DMABUF session failed" | Driver does not support DMABUF export, or device is busy |
| "No frames compared" | All capture attempts timed out — trigger not firing or camera not responding |
| Coherency failure (match_with_sync < tested) | Kernel DMA mapping bug, incorrect cache attributes on buffer allocation |
| Test skipped | Compiled on a system without `linux/dma-buf.h` — install kernel headers |
