# t01 — V4L2 Device Compliance

**Layer:** 1 — Discovery  
**Category:** discovery  
**Trigger required:** no  

## Purpose

Validates that the V4L2 device exposes the required capture and streaming capabilities, supports the selected memory backend, and enumerates pixel formats. This is the gatekeeper test — if it fails, subsequent tests cannot run reliably.

## How It Works

1. Opens the device and calls `query_device()` to obtain driver name, card name, bus info, capability flags, and enumerated pixel formats.
2. Probes all memory backends (MMAP, USERPTR, DMABUF) via `probe_memory_backends()` to determine which are accepted by `VIDIOC_REQBUFS`.
3. Checks whether the user-selected backend is among the supported ones.
4. Emits capability flags, format list, and backend probe results as metrics and details.
5. Renders a verdict based on capture/streaming support and backend compatibility.

## Implementation

Function: inline in dispatch chain (`definition.id == "t01-device-compliance"`) in [diagnostic_runner.cpp](../../../source/backend/core/src/diagnostic_runner.cpp)  
Registry: `t01-device-compliance` in [test_registry.cpp](../../../source/backend/core/src/test_registry.cpp)

> The source file contains `// Docs: docs/backend/tests/t01-device-compliance.md`
> above the function as a back-reference to this document.

## Parameters

No configurable parameters.

## Output Metrics

| Key | Unit | Description |
|-----|------|-------------|
| `backend_mmap` | bool | Whether MMAP backend is supported |
| `backend_userptr` | bool | Whether USERPTR backend is supported |
| `backend_dmabuf` | bool | Whether DMABUF backend is supported |
| `supports_capture` | bool | Whether the device reports V4L2 capture support |
| `supports_streaming` | bool | Whether the device reports V4L2 streaming support |
| `format_count` | count | Number of enumerated V4L2 pixel formats |
| `selected_backend_supported` | bool | Whether the selected memory backend is accepted by VIDIOC_REQBUFS |

## Report Details

```
mmap: supported (VIDIOC_REQBUFS granted 4 buffers)
userptr: unsupported (VIDIOC_REQBUFS returned EINVAL)
dmabuf: supported (VIDIOC_REQBUFS granted 4 buffers)
Driver: isx021
Card: ISX021 Camera
Bus: platform:capture
Selected backend: mmap
Format: YUYV (YUYV 4:2:2, single-planar)
```

## Verdict Logic

| Status | Condition |
|--------|-----------|
| **Pass** | Device supports capture, streaming, and the selected memory backend |
| **Warn** | Device supports capture/streaming but the selected backend was rejected by VIDIOC_REQBUFS |
| **Fail** | Device does not expose required V4L2 capture and streaming capabilities, or `query_device()` fails entirely |

## Interpretation Guide

- `supports_capture = 0` or `supports_streaming = 0`: The device file is not a V4L2 video capture device (may be a metadata or subdev node).
- `selected_backend_supported = 0`: Switch to a different memory backend (e.g., try MMAP instead of DMABUF).
- `format_count = 0`: The driver does not enumerate any pixel formats — firmware or driver issue.
- All `backend_*` metrics are 0: The device node may not support buffer-based I/O at all.

## Failure Modes

| Symptom | Likely Cause |
|---------|--------------|
| "Failed to query V4L2 device" | Device node doesn't exist or permissions are wrong |
| `supports_capture = 0` | Wrong `/dev/video*` node selected (subdev, metadata, etc.) |
| `selected_backend_supported = 0` | Driver doesn't support the chosen backend; try MMAP |
| `format_count = 0` | Driver/firmware initialization incomplete |
