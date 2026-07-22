# t15 — Memory access throughput

> - **Implementation:** search `run_t15` in [diagnostic_runner.cpp](../../../source/backend/core/src/diagnostic_runner.cpp)
> - **Definition:** search `t15-memory-throughput` in [test_registry.cpp](../../../source/backend/core/src/test_registry.cpp)
> - **Category:** `memory`
> - **`uses_trigger`:** no
> - **Trigger modes:** all — irrelevant, no trigger or streaming is involved

## 1. Overview / scope

**What it measures:** raw `memcpy` throughput reading directly from the
device's mapped capture buffer — at three sizes, and (when available) both
through the plain mapping and through the exported DMA-BUF mapping.

**Questions it answers:**

- How fast can the application copy data out of the device-mapped buffer,
  independent of capture timing or triggers?
- Does throughput differ between reading a small slice (4 KB, 64 KB) versus
  the full frame?
- Does reading through the DMA-BUF path (with or without explicit cache
  sync) perform differently than the plain memory-mapped path?

**Why it matters:** every other test's latency numbers include the cost of
copying frame data somewhere. This test isolates **just** the memory-copy
cost, with no streaming or trigger involved, so a slow `memcpy` can be told
apart from a slow capture pipeline.

**Method:** allocate buffers (no streaming), then repeatedly `memcpy` from
the buffer's mapped memory at three slice sizes, timing 100 repetitions each
to compute MB/s.

## 2. Trigger modes

Not applicable — this test never fires a trigger or starts streaming, so the
selected trigger mode has no effect on its behavior or results.

## 3. Inputs

All parameters are fixed in `run_t15`:

| Input | Value | Meaning |
| --- | --- | --- |
| Buffer count | `2` | Allocated via `setup_buffers` — never streamed. |
| Benchmark repetitions | `100` | Per size, per memory path. |
| Slice sizes | full frame, `4096` bytes, `65536` bytes | Each benchmarked separately. |
| Memory backend | selected run backend | `mmap` / `dmabuf` / `userptr` — determines which paths are available (see below). |

> **Preconditions:** a V4L2 device that can allocate buffers for the
> selected backend. No trigger, GPIO, or streaming camera activity is
> required.

## 4. Outputs & interpretation

### Metrics

| Metric | Unit | Meaning |
| --- | --- | --- |
| `frame_size_bytes` | bytes | Size of the device-mapped buffer. |
| `<path>_full_mbps` | MB/s | Throughput copying the entire frame from `<path>`. |
| `<path>_4k_mbps` | MB/s | Throughput copying a 4 KB slice from `<path>`. |
| `<path>_64k_mbps` | MB/s | Throughput copying a 64 KB slice from `<path>`. |
| `dmabuf_sync_mbps` | MB/s | Full-frame throughput through the DMA-BUF path **with** explicit `DMA_BUF_IOCTL_SYNC` calls around each copy. Only present when the DMA-BUF sync header is available at build time and a DMA-BUF fd was obtained. |

`<path>` is `mmap` for the `mmap` and `userptr` backends. For the `dmabuf`
backend, the plain-mapped-pointer benchmark is **also labeled `mmap`** (see
§5) and an additional `dmabuf_full`/`dmabuf_4k`/`dmabuf_64k` set is
benchmarked separately through the DMA-BUF-exported pointer.

### Status

| Status | Condition | Meaning |
| --- | --- | --- |
| `Pass` | buffers were allocated and at least one mapped buffer exists | Always — this test characterizes throughput, it does not assert a specific speed is "correct". |
| `Error` | buffer setup failed, or no mapped buffer is available | Setup problem, not a result (see summary). |

There is no `Fail`/`Warn` — like [t06](t06-format-comparison.md), this test
reports raw numbers for you to compare, not a verdict.

### How to read the numbers

- **Full-frame vs. slice throughput** — if 4 KB/64 KB throughput is much
  higher than full-frame throughput, cache effects are likely dominating the
  smaller copies; a roughly flat MB/s across sizes suggests the copy is
  bandwidth-bound rather than cache-bound.
- **`mmap` vs `dmabuf_*` (dmabuf backend only)** — compares reading through
  the plain CPU mapping against reading through the DMA-BUF-exported
  mapping of the *same underlying buffer*. A large gap indicates the export
  path adds real overhead (or benefit) beyond a plain mmap read.
- **`dmabuf_sync_mbps` vs `dmabuf_full_mbps`** — the difference is the cost
  of the explicit `DMA_BUF_IOCTL_SYNC` cache-coherency calls. If this metric
  is absent, either the platform lacks `linux/dma-buf.h` support at build
  time, or no DMA-BUF fd was available — see
  [t18](t18-dmabuf-cache-sync.md), which uses the same sync mechanism to
  test cache *coherency* (correctness) rather than throughput.
- **No absolute "good" MB/s threshold** — this depends entirely on the
  platform's memory subsystem; compare across backends/paths and across
  runs on the same hardware, not against a fixed target.

## 5. How the code works

`run_t15`:

1. **Allocate, don't stream** — opens the device and calls
   `setup_buffers(2, backend)` — buffers are requested and mapped, but
   `STREAMON` is never called. Failure, or an unmapped first buffer, yields
   `Error`.
2. **`bench` helper** — a local lambda that times `REPS=100` back-to-back
   `memcpy` calls from a given source pointer/size into a scratch
   destination buffer, computes MB/s, and pushes both a metric and a
   `details` line.
3. **Plain-mapped-pointer pass** — benchmarks `<prefix>_full`/`_4k`/`_64k`
   against `s.buffers()[0].data()`. The label prefix is `to_string(backend)`
   normally, but is deliberately relabeled `"mmap"` when `backend ==
   MemoryBackend::Dmabuf` — because the DMA-BUF backend still negotiates its
   underlying buffer as a plain mmap mapping before exporting it, so
   labeling it `"dmabuf"` here would collide with the separate DMA-BUF-fd
   benchmark that follows.
4. **DMA-BUF pointer pass** (only if `s.buffers()[0].dma_start` is non-null)
   — benchmarks `dmabuf_full`/`_4k`/`_64k` against the DMA-BUF-exported
   pointer for the same buffer.
5. **DMA-BUF sync pass** (only if built with DMA-BUF sync support and a
   valid DMA-BUF fd exists) — repeats the full-frame copy `REPS` times, this
   time wrapping each copy with `DMA_BUF_IOCTL_SYNC` start/end calls, and
   pushes `dmabuf_sync_mbps`.
6. **Verdict** — always `Pass` once setup succeeds.

This is the only test with no `TriggerSource` parameter at all in its
signature — it neither fires a trigger nor calls `STREAMON`, making it the
one test in the suite that can run meaningfully with **zero** camera motion.
