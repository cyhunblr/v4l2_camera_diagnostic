# t10 — Memory Access Throughput

**Layer:** 3 — Buffer & memory  
**Category:** memory  
**Trigger required:** no  

## Purpose

Benchmarks the raw memcpy throughput from device-mapped V4L2 buffers to user-space memory. This measures the practical memory bandwidth available for frame data transfer, providing a baseline for understanding whether the memory subsystem can sustain the required frame rate at the configured resolution. Results help identify bottlenecks in DMA paths and cache-coherent vs non-coherent memory regions.

## How It Works

1. Opens the camera device and allocates 2 buffers in the configured memory backend (mmap or dmabuf) without starting the stream.
2. Determines the frame size from the first allocated buffer.
3. Runs a memcpy benchmark on the full frame, 4 KB subset, and 64 KB subset of the primary buffer mapping.
4. If the backend is dmabuf and a DMA mapping is available, repeats the same 3 benchmarks on the DMA pointer.
5. If `DMA_BUF_IOCTL_SYNC` is available, benchmarks full-frame memcpy bracketed by sync start/end ioctls.
6. Records throughput in MB/s for each benchmark variant.

## Implementation

Function: `run_memory_throughput` in [diagnostic_runner.cpp](../../../source/backend/core/src/diagnostic_runner.cpp)  
Registry: `t10-memory-throughput` in [test_registry.cpp](../../../source/backend/core/src/test_registry.cpp)

> The source file contains `// Docs: docs/backend/tests/t10-memory-throughput.md`
> above the function as a back-reference to this document.

## Parameters

| Key | Default | Unit | Description |
|-----|---------|------|-------------|
| `benchmark_reps` | 100 | count | Number of memcpy repetitions per benchmark variant |

## Output Metrics

### Fixed metrics

| Metric Key | Unit | Description |
|-----------|------|-------------|
| `frame_size_bytes` | bytes | Size of a single device buffer (frame) |

### Primary buffer metrics (mmap or dmabuf-as-mmap)

The prefix is `mmap` when the backend is Dmabuf, or the backend name (e.g. `mmap`) otherwise.

| Metric Key Pattern | Unit | Description |
|-------------------|------|-------------|
| `{prefix}_full_mbps` | MB/s | Full-frame memcpy throughput |
| `{prefix}_4k_mbps` | MB/s | 4 KB memcpy throughput |
| `{prefix}_64k_mbps` | MB/s | 64 KB memcpy throughput |

### DMA buffer metrics (only when dmabuf mapping available)

| Metric Key | Unit | Description |
|-----------|------|-------------|
| `dmabuf_full_mbps` | MB/s | Full-frame memcpy from DMA pointer |
| `dmabuf_4k_mbps` | MB/s | 4 KB memcpy from DMA pointer |
| `dmabuf_64k_mbps` | MB/s | 64 KB memcpy from DMA pointer |
| `dmabuf_sync_mbps` | MB/s | Full-frame memcpy bracketed by DMA_BUF_IOCTL_SYNC (only if linux/dma-buf.h available) |

## Report Details

```
Buffer size: 3686400 bytes
mmap_full: 4521 MB/s
mmap_4k: 12043 MB/s
mmap_64k: 8922 MB/s
dmabuf_full: 2103 MB/s
dmabuf_4k: 9845 MB/s
dmabuf_64k: 5421 MB/s
```

## Verdict Logic

| Status | Condition |
|--------|-----------|
| **Pass** | Benchmark completes successfully on at least the primary buffer |
| **Fail** | Buffer setup fails or no mapped buffer data is available |

This test does not use warn/fail thresholds on throughput values — it is an informational benchmark.

## Interpretation Guide

- **mmap throughput ≫ dmabuf throughput**: The DMA path traverses an uncached or write-combining region; consider using mmap for frame access.
- **dmabuf_sync_mbps significantly lower than dmabuf_full_mbps**: The DMA_BUF_IOCTL_SYNC overhead is non-trivial; cache invalidation is expensive on this platform.
- **4k throughput ≫ full-frame throughput**: Large frames exceed LLC (last-level cache) capacity; performance is memory-bound at full resolution.
- **All throughputs similar**: Memory subsystem is uniform — no special DMA path optimizations needed.
- **Very low throughput (< 500 MB/s)**: Possible uncached mapping or memory-mapped I/O region without write-combining.

## Failure Modes

| Symptom | Likely Cause |
|---------|--------------|
| "Buffer setup failed" | Device busy, insufficient memory, or invalid backend for this driver |
| "No mapped buffers available" | Driver allocated buffers but mmap returned NULL (kernel/driver bug) |
| Missing dmabuf metrics | Backend is not Dmabuf or driver does not export DMA-BUF file descriptors |
| Missing dmabuf_sync_mbps | Compiled without `linux/dma-buf.h` header support |
