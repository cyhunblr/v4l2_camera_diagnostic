# t18 — DMA_BUF_IOCTL_SYNC cache coherency

> - **Implementation:** search `run_t18` in [diagnostic_runner.cpp](../../../source/backend/core/src/diagnostic_runner.cpp)
> - **Definition:** search `t18-dmabuf-cache-sync` in [test_registry.cpp](../../../source/backend/core/src/test_registry.cpp)
> - **Category:** `dmabuf`
> - **`uses_trigger`:** yes
> - **Trigger modes:** all (Hardware, Software, FreeRun)
> - **`requires_dmabuf`:** yes — the **only** test with this memory-backend restriction

## 1. Overview / scope

**What it checks:** whether reading a DMA-BUF-exported buffer through its
CPU mapping can return **stale, cached data** compared to the same physical
buffer's plain `mmap` view — and whether explicit `DMA_BUF_IOCTL_SYNC` calls
fix that.

**Questions it answers:**

- Does the DMA-BUF-mapped pointer ever disagree with the plain-mmap pointer
  for the same underlying buffer, without any cache synchronization?
- Does wrapping the read with `DMA_BUF_SYNC_START`/`DMA_BUF_SYNC_END` make
  the two views agree?
- Is explicit sync actually **required** on this platform, or is the DMA
  coherent by default?

**Why it matters:** DMA-BUF buffers can be accessed by both the CPU and DMA
hardware, and CPU caches can hold a stale view unless explicitly
synchronized. This test directly verifies that correctness property rather
than assuming it — and this is a **correctness** check, distinct from
[t15](t15-memory-throughput.md)'s DMA-BUF *throughput* benchmark, which
uses the same sync mechanism but never checks whether the data it reads is
actually correct.

**Method:** for 20 triggered frames, dequeue without requeuing, then compare
three views of the **same buffer**: the plain `mmap` pointer, the DMA-BUF
pointer read with no synchronization, and the DMA-BUF pointer read wrapped
in `DMA_BUF_IOCTL_SYNC` calls.

## 2. Trigger modes

No restriction — cache coherency is checked the same way regardless of
trigger source.

## 3. Inputs

All parameters are fixed in `run_t18`:

| Input | Value | Meaning |
| --- | --- | --- |
| Frames tested | `20` | Triggered captures attempted. |
| Bytes compared | `64` | Leading bytes of each buffer compared across views. |
| Buffer count | `2` | Forced `MemoryBackend::Dmabuf` regardless of the run's selected backend. |
| Capture timeout | `100 ms` | Per sample. |
| Inter-sample interval | `100 ms` | Between samples. |

> **Preconditions:** a device supporting the DMA-BUF backend, plus a
> working trigger for the selected mode. Also requires the build to have
> `linux/dma-buf.h` available (see §4 "Compile-time skip").

## 4. Outputs & interpretation

### Metrics

| Metric | Unit | Meaning |
| --- | --- | --- |
| `frames_tested` | count | Frames where all three views (mmap, dma-nosync, dma-sync) could be compared. |
| `match_without_sync` | count | Frames where the DMA-BUF view **without** sync already matched the mmap view. |
| `match_with_sync` | count | Frames where the DMA-BUF view **with** sync matched the mmap view. |
| `sync_required` | bool | `true` if at least one no-sync mismatch occurred **and** every sync'd comparison matched — i.e. sync measurably fixed a real discrepancy. |

### Status

| Status | Condition | Meaning |
| --- | --- | --- |
| `Pass` | `match_with_sync == frames_tested` | With sync, every tested frame's DMA-BUF view agreed with the mmap view — cache coherency holds once sync is applied (regardless of whether sync was actually necessary). |
| `Fail` | `match_with_sync < frames_tested` | Even **with** explicit sync, some frames disagreed between views — a real cache-coherency problem. |
| `Error` | DMABUF session failed, or zero frames were comparable | Setup problem, or no valid frame pairs to compare (see summary). |
| `Skipped` (runtime) | `requires_dmabuf` and a non-DMABUF backend was selected | Expected — see the Trigger modes section in [docs/testing.md](../../testing.md#trigger-modes). |
| `Skipped` (compile-time) | `linux/dma-buf.h` unavailable at build time | The entire test body is compiled out; reported as "linux/dma-buf.h not available..." regardless of backend selection. |

### How to read the numbers

- **`sync_required == true`** means this platform's DMA is **not** coherent
  by default — applications reading DMA-BUF buffers on this hardware must
  perform explicit sync, or they risk reading stale cached data.
  `sync_required == false` alongside `Pass` means the platform happens to be
  coherent already, but does not mean sync calls are unsafe to keep — they
  are a no-op cost in that case, not a correctness risk either way.
- **`match_without_sync == frames_tested`** (no mismatches even without
  sync) — combined with `Pass`, this is the "coherent DMA" case reported
  directly in the summary.
- **`Fail`** (sync did not fix the mismatch) is the serious outcome — it
  means the `DMA_BUF_IOCTL_SYNC` mechanism itself is not producing correct
  results on this platform, which is a deeper problem than simply needing to
  remember to call it.

## 5. How the code works

`run_t18`:

1. **Compile-time gate** — the entire function body is wrapped in
   `#if !V4L2DIAG_HAS_DMA_BUF_SYNC` / `#else`; if the DMA-BUF sync header
   was not available at build time, the function immediately reports
   `Skipped` and nothing else runs.
2. **Open + start + warm-up** — opens the device and forces
   `MemoryBackend::Dmabuf` regardless of the run's selected backend
   (this test's whole purpose requires DMA-BUF). Failure yields `Error`.
3. **Per-frame comparison loop** (20 iterations): captures with drain-only
   (no auto-requeue, so the buffer stays valid for inspection), skips frames
   that are too small or missing a valid DMA-BUF pointer/fd (requeuing them
   immediately). For valid frames:
   - Copies the first 64 bytes from the **plain mmap pointer** and from the
     **DMA-BUF pointer with no sync**, comparing them (`match_nosync`).
   - Wraps a fresh 64-byte read of the **DMA-BUF pointer** in
     `DMA_BUF_SYNC_START`/`END` (`DMA_BUF_IOCTL_SYNC`), comparing that
     against the mmap view (`match_sync`).
   - Requeues the buffer.
4. **Aggregate** — pushes `frames_tested`, `match_without_sync`,
   `match_with_sync`, and the derived `sync_required` boolean.
5. **Verdict** — `Error` if no frames were comparable; `Pass` if every
   sync'd comparison matched; `Fail` otherwise.

This is the only test forcing `MemoryBackend::Dmabuf` internally rather than
using the run's selected backend — combined with the registry's
`requires_dmabuf` flag, this means the test is meaningful only when DMA-BUF
is both selected for the run **and** actually available on the device.
