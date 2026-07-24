# t03 — Frame Capture Without STREAMON

**Layer:** 2 — State-machine correctness  
**Category:** stream-state  
**Trigger required:** no  

## Purpose

Validates that the V4L2 driver correctly prevents frame delivery before `VIDIOC_STREAMON` is called. A compliant driver must not deliver frames until streaming is explicitly started — violation of this invariant indicates a broken state machine in the kernel driver.

## How It Works

1. Opens the device and allocates buffers via `VIDIOC_REQBUFS` (without calling `VIDIOC_STREAMON`).
2. Calls `poll()` with a 50ms timeout to check if the driver falsely reports data ready.
3. Attempts `VIDIOC_DQBUF` to see if a frame can be dequeued without streaming.
4. Records the poll return value, whether DQBUF failed, and the errno from DQBUF.
5. A correct driver should return poll=0 (timeout, no data) and DQBUF should fail with EAGAIN or EINVAL.

## Implementation

Function: `run_no_streamon` in [diagnostic_runner.cpp](../../../source/backend/core/src/diagnostic_runner.cpp)  
Registry: `t03-no-streamon` in [test_registry.cpp](../../../source/backend/core/src/test_registry.cpp)

> The source file contains `// Docs: docs/backend/tests/t03-no-streamon.md`
> above the function as a back-reference to this document.

## Parameters

| Key | Default | Unit | Description |
|-----|---------|------|-------------|
| `buffer_count` | 2 | count | Number of buffers to request from the driver |
| `poll_timeout_ms` | 50 | ms | Timeout for the poll() call |

## Output Metrics

| Key | Unit | Description |
|-----|------|-------------|
| `poll_returned` | count | Return value from `poll(50ms)` without STREAMON; expected 0 (timeout) |
| `dqbuf_failed` | bool | Whether DQBUF correctly failed without STREAMON (1=correct, 0=violation) |
| `dqbuf_errno` | errno | errno from the DQBUF attempt; expected EAGAIN (11) or EINVAL (22) |

## Report Details

```
poll(50ms) returned 0 (expected 0)
DQBUF returned -1, errno=11 (Resource temporarily unavailable)
```

Or on failure:
```
poll(50ms) returned 1 (expected 0)
DQBUF unexpectedly succeeded, sequence=0
```

## Verdict Logic

| Status | Condition |
|--------|-----------|
| **Pass** | poll returns 0 AND DQBUF fails with EAGAIN or EINVAL |
| **Warn** | DQBUF correctly fails but poll returned non-zero (driver signals readiness it shouldn't) |
| **Fail** | DQBUF succeeded without STREAMON — V4L2 state machine violation |

## Interpretation Guide

- `poll_returned = 0` and `dqbuf_failed = 1`: Driver correctly blocks delivery before STREAMON — healthy behavior.
- `poll_returned > 0`: The driver's poll implementation incorrectly signals data availability. This may be a spurious wakeup or a driver bug.
- `dqbuf_errno = 11 (EAGAIN)`: Standard response from a non-blocking fd with no buffer ready.
- `dqbuf_errno = 22 (EINVAL)`: Some drivers return EINVAL instead of EAGAIN when not streaming — both are acceptable.
- `dqbuf_failed = 0`: Critical failure — driver delivers frames before STREAMON, violating the V4L2 specification.

## Failure Modes

| Symptom | Likely Cause |
|---------|--------------|
| "Failed to open device" | Wrong device path or permission denied |
| "VIDIOC_REQBUFS failed" | Driver cannot allocate memory buffers (memory pressure or unsupported backend) |
| DQBUF succeeded | Kernel driver bug — frames queued/delivered without STREAMON |
| poll returns 1 but DQBUF fails | Minor driver issue — poll revents set incorrectly but data flow is correct |
