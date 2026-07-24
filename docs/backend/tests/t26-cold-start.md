# t26 — Cold Start

**Layer:** 7 — Stability  
**Category:** stability  
**Trigger required:** yes  

## Purpose

Characterises how many frames a camera needs after a fresh open/start cycle before its capture latency stabilises to steady-state levels. Many cameras and drivers exhibit elevated latency or missed frames in the first few captures after STREAMON (sensor auto-exposure settling, ISP pipeline fill, DMA buffer priming). This test quantifies that warm-up cost across multiple independent cycles to determine how many frames should be discarded before trusting latency measurements.

## How It Works

1. Repeats `cycles` independent cold-start sessions:
   - Opens a fresh V4L2 session (2 buffers) — no warmup is performed.
   - Captures up to `max_frames_per_cycle` frames, recording latency for each (or −1 for misses).
   - Determines the **warmup point**: the first frame index from which all subsequent good frames remain within `stability_threshold_pct`% of the tail (last 5 frames) mean.
   - Closes the session completely before the next cycle.
2. Collects warmup frame counts across all cycles.
3. Computes mean and max warmup counts.
4. Renders a verdict based on how quickly the camera stabilises.

## Implementation

Function: `run_cold_start` in [diagnostic_runner.cpp](../../../source/backend/core/src/diagnostic_runner.cpp)  
Registry: `t26-cold-start` in [test_registry.cpp](../../../source/backend/core/src/test_registry.cpp)

> The source file contains `// Docs: docs/backend/tests/t26-cold-start.md`
> above the function as a back-reference to this document.

## Parameters

| Key | Default | Unit | Description |
|-----|---------|------|-------------|
| `cycles` | 10 | count | Number of independent open→capture→close cycles. |
| `max_frames_per_cycle` | 30 | count | Maximum frames captured per cycle to search for stabilisation. |
| `stability_threshold_pct` | 15.0 | % | Allowable deviation from steady-state mean to consider a frame "stable". |

## Output Metrics

| Metric Key | Unit | Description |
|-----------|------|-------------|
| `warmup_mean_frames` | count | Mean number of frames before latency stabilises (averaged over all cycles). |
| `warmup_max_frames` | count | Worst-case warmup — the cycle with the most frames before reaching stability. |
| `cycles_completed` | count | Number of cycles where a session was successfully opened and frames captured. |

## Report Details

Per-cycle lines showing the warmup frame count:

```
cycle  1: warmup=3 frames
cycle  2: warmup=2 frames
cycle  3: warmup=4 frames
cycle  4: warmup=3 frames
...
```

If a cycle fails to open, a detail line reports the error:

```
cycle  5: session failed — Cannot open device: Device or resource busy
```

## Verdict Logic

| Status | Condition |
|--------|-----------|
| **Pass** | `warmup_mean_frames ≤ 3` — "Fast warm-up" |
| **Pass** | `warmup_mean_frames ≤ 10` — "Moderate warm-up" |
| **Warn** | `warmup_mean_frames > 10` — "Slow warm-up" |
| **Fail** | All cycles failed to open a session. |

Note: This test does not have threshold-based Pass/Fail from the threshold registry — it uses hard-coded warm-up quality bands.

## Interpretation Guide

- **`warmup_mean_frames` ≤ 3** — camera stabilises almost immediately; other tests can safely discard just 3–5 warmup frames.
- **`warmup_mean_frames` 4–10** — moderate warm-up typical of sensors with auto-exposure or auto-gain loops; configure other tests to warm up at least this many frames.
- **`warmup_mean_frames` > 10** — slow stabilisation; possibly a high-resolution sensor with a deep ISP pipeline, or a sensor that takes many frames to converge exposure.
- **`warmup_max_frames` >> `warmup_mean_frames`** — high variance between cycles; the warm-up duration is unpredictable, suggesting a non-deterministic sensor initialisation (e.g. exposure hunting under varying light).
- **`warmup_max_frames = max_frames_per_cycle`** — the camera never stabilised within the capture window. Either the threshold is too tight or the camera has an inherent drift.
- **`cycles_completed < cycles`** — some sessions failed to open; may indicate device contention or a driver that doesn't cleanly release resources between rapid open/close cycles.

## Failure Modes

| Symptom | Likely Cause |
|---------|--------------|
| `warmup_mean_frames` equals `max_frames_per_cycle` | Camera never reaches stable latency within 30 frames. Increase `max_frames_per_cycle`, loosen `stability_threshold_pct`, or investigate sensor auto-exposure oscillation. |
| All cycles fail ("session failed") | Device not releasing resources between cycles; driver may need time between close and re-open. Could also be a permission issue or device removed. |
| High variance in warmup counts | Non-deterministic sensor behaviour (auto-exposure convergence depends on scene content at start); test under controlled lighting for repeatable results. |
| `warmup_mean_frames = 0` | Every frame from the very first is stable. Camera has no warm-up period — ideal behaviour, or `stability_threshold_pct` is too loose to detect initial transient. |
| Several cycles report `warmup = max_frames_per_cycle` while others are low | Intermittent hardware issue (loose connector, thermal throttle) that only sometimes manifests during cold start. |
