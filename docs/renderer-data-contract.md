# Renderer veri sozlesmesi — 26 test

Kaynak: `docs/assets/refactored_previews/t01..t26-preview.html`
(tasarim onaylandi 2026-08-07).

Bu belge, her testin renderer'inin **uretmesi gereken** bolumleri, tablolari,
grafikleri ve metric adlarini listeler. Mevcut renderer ciktisiyla
karsilastirma **yapilmamistir** — hedef dogrudan preview'lerdir.

Uc kart = uc trigger modu. Bolum kumesi moda gore degisebilir; asagidaki
tablolar her modu ayri gosterir.

Sutun imzalari ve `Unit` sozlugu icin bkz.
`docs/report-ui-design-spec.md` §"Section ve Grafik Sozlesmesi" (S1-S10).

Iki tablo bicimi vardir ve ikisi de gecerlidir: `X-header` + `X-row`
(24 test) ve `X-header-row` + `X-row` (T04, T05).

---

## T01 — V4L2 Device Compliance

| mod | status | bolumler |
| --- | --- | --- |
| Free-run | PASS | Device Evidence · Information › Device Evidence · Capability › Device Evidence · Pixel Formats |
| Hardware-trigger | PASS | Device Evidence · Information › Device Evidence · Capability › Device Evidence · Pixel Formats |
| Software-trigger | PASS | Device Evidence · Information › Device Evidence · Capability › Device Evidence · Pixel Formats |

Alanlar (Free-run karti):

`.kv` tanim listesi (0 alan):

`.fmt` format satiri: 1 adet

---

## T02 — V4L2 Control Inventory

| mod | status | bolumler |
| --- | --- | --- |
| Free-run | PASS | Control Evidence |
| Hardware-trigger | PASS | Control Evidence |
| Software-trigger | PASS | Control Evidence |

Alanlar (Free-run karti):

`.ctrl` · `Control | ID | Access | Range | Step | Default | Current` · 4 satir

  Per-item satir; ilk sutun `Control`.

---

## T03 — Pipeline Readiness after STREAMON

| mod | status | bolumler |
| --- | --- | --- |
| Free-run | PASS | Measurement › Timing by cycle › grafik › Aggregate › Measurement Result › Test Configuration |
| Hardware-trigger | PASS | Measurement › Timing by cycle › grafik › Aggregate › Measurement Result › Test Configuration |
| Software-trigger | PASS | Measurement › Timing by cycle › grafik › Aggregate › Measurement Result › Test Configuration |

Alanlar (Free-run karti):

`.magg` · `Metric | Type | Unit | Value | Detail` · 2 satir

- STREAMON mean
- First-frame mean

`.tsum` · `Metric | Type | Unit | Value | Detail | Status` · 4 satir

- STREAMON max
- First-frame max
- Completed cycles
- Timeouts

`.config` · `Variable | Source | Type | Unit | Value` · 8 satir

- Cycles
- Buffer count
- First-frame deadline
- Settle time
- Slow-start guard
- PASS threshold
- WARN threshold
- Backend memory

`.cycle` CSS bar grafigi · 3 bar

- Cycle 1
- Cycle 2
- Cycle 3

Grafik: 1 `stacked-chart` CSS bar

---

## T04 — Frame capture without STREAMON

| mod | status | bolumler |
| --- | --- | --- |
| Free-run | FAIL | Measurement › Measurement Result › Test Configuration |
| Hardware-trigger | WARN | Measurement › Measurement Result › Test Configuration |
| Software-trigger | WARN | Measurement › Measurement Result › Test Configuration |

Alanlar (Free-run karti):

`.check` · `Phase | Expected | Observed | Detail` · 2 satir

- poll() before STREAMON
- DQBUF before STREAMON

`.cres` · `Metric | Type | Unit | Value | Detail | Status` · 2 satir

- Conformant phases
- Pre-STREAMON frames

`.config` · `Variable | Source | Type | Unit | Value` · 3 satir

- Buffer count
- Poll timeout
- Backend memory

---

## T05 — STREAMOFF Error Handling and Recovery

| mod | status | bolumler |
| --- | --- | --- |
| Free-run | FAIL | Measurement › Measurement Result › Test Configuration |
| Hardware-trigger | FAIL | Measurement › Measurement Result › Test Configuration |
| Software-trigger | FAIL | Measurement › Measurement Result › Test Configuration |

Alanlar (Free-run karti):

`.check` · `Phase | Expected | Observed | Detail` · 6 satir

- Baseline capture
- STREAMOFF
- Poll after stop
- DQBUF after stop
- Re-STREAMON
- Recovery capture

`.cres` · `Metric | Type | Unit | Value | Detail | Status` · 3 satir

- Conformant phases
- Post-STREAMOFF frames
- Recovery captures

`.config` · `Variable | Source | Type | Unit | Value` · 6 satir

- Baseline captures
- Recovery captures
- Warmup count
- Minimum recovery
- Poll timeout
- Backend memory

---

## T06 — STREAMON/STREAMOFF Cycle Reliability

| mod | status | bolumler |
| --- | --- | --- |
| Free-run | PASS | Measurement › Cycle reliability › Aggregate › Measurement Result › Test Configuration |
| Hardware-trigger | FAIL | Measurement › Cycle reliability › Aggregate › Measurement Result › Test Configuration |
| Software-trigger | FAIL | Measurement › Cycle reliability › Aggregate › Measurement Result › Test Configuration |

Alanlar (Free-run karti):

`.magg` · `Metric | Type | Unit | Value | Detail` · 4 satir

- Open + STREAMON mean
- Open + STREAMON maximum
- Measured capture mean
- Measured capture maximum

`.tsum` · `Metric | Type | Unit | Value | Detail | Status` · 6 satir

- Full cycle completion
- Rapid cycle completion
- Full phase
- Rapid phase
- Start failures
- Slow-start guard

`.config` · `Variable | Source | Type | Unit | Value` · 13 satir

- Full cycles
- Rapid cycles
- Full warmup
- Rapid warmup
- Full timeout
- Rapid timeout
- Rapid pacing
- Slow-start guard
- Full pass threshold
- Full warn threshold
- Rapid pass threshold
- Rapid warn threshold
- Backend memory

`.rel` CSS bar grafigi · 2 bar

- Full cycles
- Rapid cycles

---

## T07 — Multi-buffer Configurations

| mod | status | bolumler |
| --- | --- | --- |
| Free-run | PASS | Measurement › Latency by buffer count › Aggregate › Measurement Result › Test Configuration |
| Hardware-trigger | PASS | Measurement › Latency by buffer count › Aggregate › Measurement Result › Test Configuration |
| Software-trigger | PASS | Measurement › Latency by buffer count › Aggregate › Measurement Result › Test Configuration |

Alanlar (Free-run karti):

`.cfgres` · `Requested | Allocated | Captured | Mean latency (ms) | Detail` · 5 satir

  Per-item satir; ilk sutun `Requested`.

`.magg` · `Metric | Type | Unit | Value | Detail` · 2 satir

- Buffer counts tested
- Mean latency

`.mres` · `Metric | Type | Unit | Value | Detail | Status` · 3 satir

- Allocation honored
- Capture success
- Latency spread

`.config` · `Variable | Source | Type | Unit | Value` · 5 satir

- Sample count
- Max buffers
- Warmup count
- Capture timeout
- Sample interval

---

## T08 — Buffer Saturation Behavior

| mod | status | bolumler |
| --- | --- | --- |
| Free-run | SKIP | _(icerik yok)_ |
| Hardware-trigger | WARN | Measurement › Saturation by variant › Buffer state after saturation › Aggregate › Measurement Result › Test Configuration |
| Software-trigger | WARN | Measurement › Saturation by variant › Buffer state after saturation › Aggregate › Measurement Result › Test Configuration |

Alanlar (Hardware-trigger karti):

`.variant` · `Variant | Trigger load | Allocated | Available | Error flagged | Detail` · 2 satir

  Per-item satir; ilk sutun `Variant`.

`.magg` · `Metric | Type | Unit | Value | Detail` · 3 satir

- Variants tested
- Buffers per variant
- Error flag mask

`.mres` · `Metric | Type | Unit | Value | Detail | Status` · 3 satir

- Buffer retention
- Error-flagged
- Variants passed

`.config` · `Variable | Source | Type | Unit | Value` · 7 satir

- Buffer count
- Variant A triggers
- Variant A interval
- Variant B triggers
- Variant B interval
- Settle time
- Max error flags

`.queue` CSS bar grafigi · 2 bar

- Variant A
- Variant B

---

## T09 — Buffer Requeue Delay Tolerance

| mod | status | bolumler |
| --- | --- | --- |
| Free-run | WARN | Measurement › Mean wait by requeue delay › grafik › Wait time by requeue delay › Aggregate › Measurement Result › Test Configuration |
| Hardware-trigger | SKIP | _(icerik yok)_ |
| Software-trigger | SKIP | _(icerik yok)_ |

Alanlar (Free-run karti):

`.delay` · `Delay | Available | Mean wait | Detail` · 12 satir

  Per-item satir; ilk sutun `Delay`.

`.magg` · `Metric | Type | Unit | Value | Detail` · 3 satir

- Delays tested
- Mean wait maximum
- Mean wait minimum

`.mres` · `Metric | Type | Unit | Value | Detail | Status` · 4 satir

- Delays passed
- Capture success
- Cliff delay
- Safe cliff margin

`.config` · `Variable | Source | Type | Unit | Value` · 5 satir

- Reps per delay
- Capture timeout
- Inter-rep interval
- Warmup count
- Min safe cliff delay

Grafik: 1 SVG (kanonik geometri, bkz. S8)

---

## T10 — V4L2 Buffer Flag Analysis

| mod | status | bolumler |
| --- | --- | --- |
| Free-run | PASS | Measurement › Flag state across samples › Decoded buffer flags › Aggregate › Measurement Result › Test Configuration |
| Hardware-trigger | PASS | Measurement › Flag state across samples › Decoded buffer flags › Aggregate › Measurement Result › Test Configuration |
| Software-trigger | PASS | Measurement › Flag state across samples › Decoded buffer flags › Aggregate › Measurement Result › Test Configuration |

Alanlar (Free-run karti):

`.flag` · `Group | Flag | Observed | Meaning | Detail` · 4 satir

  Per-item satir; ilk sutun `Group`.

`.dec` · `Metric | Type | Unit | Value | Detail` · 2 satir

- Decoded flags
- Raw value

`.magg` · `Metric | Type | Unit | Value | Detail` · 3 satir

- Samples analyzed
- Flags tracked
- Declared clock type

`.mres` · `Metric | Type | Unit | Value | Detail | Status` · 3 satir

- Capture completeness
- Error-flagged frames
- Source consistency

`.config` · `Variable | Source | Type | Unit | Value` · 5 satir

- Sample count
- Capture timeout
- Sample interval
- Warmup count
- Max error flags

---

## T11 — Memory Access Throughput

| mod | status | bolumler |
| --- | --- | --- |
| Free-run | PASS | Measurement › Throughput by copy size › Copy region evidence › Image buffer › Aggregate › Measurement Result › Test Configuration |
| Hardware-trigger | PASS | Measurement › Throughput by copy size › Copy region evidence › Image buffer › Aggregate › Measurement Result › Test Configuration |
| Software-trigger | PASS | Measurement › Throughput by copy size › Copy region evidence › Image buffer › Aggregate › Measurement Result › Test Configuration |

Alanlar (Free-run karti):

`.thr` · `Copy region | Bytes per copy | Throughput | Relative to full | Detail` · 3 satir

  Per-item satir; ilk sutun `Copy region`.

`.buf` · `Metric | Type | Unit | Value | Detail` · 3 satir

- Active image payload
- Mapped buffer capacity
- Allocation overhead

`.magg` · `Metric | Type | Unit | Value | Detail` · 3 satir

- Full-frame throughput
- Estimated copy time per buffer
- Theoretical copies per second

`.mres` · `Metric | Type | Unit | Value | Detail | Status` · 3 satir

- Copy regions tested
- Full-frame throughput
- Buffer utilization

`.config` · `Variable | Source | Type | Unit | Value` · 6 satir

- Backend memory
- Allocated buffers
- Minimum repetitions
- Target sample time
- Timer
- Stream state

`.bar` CSS bar grafigi · 3 bar

- Full frame
- 4 KiB
- 64 KiB

---

## T12 — DMABUF CPU Read Synchronization

| mod | status | bolumler |
| --- | --- | --- |
| Free-run | PASS | Measurement › Comparison evidence › Aggregate › Measurement Result › Test Configuration |
| Hardware-trigger | PASS | Measurement › Comparison evidence › Aggregate › Measurement Result › Test Configuration |
| Software-trigger | SKIP | _(icerik yok)_ |

Alanlar (Free-run karti):

`.cev` · `Metric | Type | Unit | Value | Detail` · 5 satir

- Frames compared
- Sync start
- Sync end
- Synchronized match
- Unsynchronized match

`.magg` · `Metric | Type | Unit | Value | Detail` · 2 satir

- Frames requested
- Compared data

`.mres` · `Metric | Type | Unit | Value | Detail | Status` · 3 satir

- Synchronized match
- SYNC ioctl errors
- Capture failures

`.config` · `Variable | Source | Type | Unit | Value` · 5 satir

- Requested samples
- Compared data
- Warmup frames
- Capture timeout
- Buffer count

---

## T13 — Poll Timeout Reliability Boundary

| mod | status | bolumler |
| --- | --- | --- |
| Free-run | FAIL | Measurement › Capture success by poll timeout › grafik › Timeout budget › Round evidence › Aggregate › Measurement Result › Test Configuration |
| Hardware-trigger | WARN | Measurement › Capture success by poll timeout › grafik › Timeout budget › Round evidence › Aggregate › Measurement Result › Test Configuration |
| Software-trigger | WARN | Measurement › Capture success by poll timeout › grafik › Timeout budget › Round evidence › Aggregate › Measurement Result › Test Configuration |

Alanlar (Free-run karti):

`.stab` · `Round | At cliff · 95 ms | Below cliff · 94 ms | Detail` · 5 satir

- 1
- 2
- 3
- 4
- 5

`.magg` · `Metric | Type | Unit | Value | Detail` · 3 satir

- Reliable cliff
- First miss
- Production timeout

`.mres` · `Metric | Type | Unit | Value | Detail | Status` · 3 satir

- Safety margin
- Boundary stability
- Timeout headroom

`.config` · `Variable | Source | Type | Unit | Value` · 7 satir

- Probe samples per timeout
- Stability rounds
- Frames per round
- Warmup frames
- Configured safe margin
- Production timeout
- Backend memory

`.budget` CSS bar grafigi · 4 bar

- Production
- First miss
- Reliable cliff
- Minimum target

Grafik: 1 SVG (kanonik geometri, bkz. S8)

---

## T14 — Trigger-to-Frame Delivery Latency

| mod | status | bolumler |
| --- | --- | --- |
| Free-run | SKIP | _(icerik yok)_ |
| Hardware-trigger | PASS | Measurement › Latency distribution › grafik › Capture timeout headroom › Latency evidence › Variability › Measurement Result › Test Configuration |
| Software-trigger | PASS | Measurement › Latency distribution › grafik › Capture timeout headroom › Latency evidence › Variability › Measurement Result › Test Configuration |

Alanlar (Hardware-trigger karti):

`.lat` · `Metric | Type | Unit | Value | Detail` · 4 satir

- Minimum
- Mean
- P95
- Maximum

`.var` · `Metric | Type | Unit | Value | Detail` · 3 satir

- Standard deviation
- Inter-sample delta variation
- Min-max spread

`.mres` · `Metric | Type | Unit | Value | Detail | Status` · 3 satir

- Capture reliability
- Missed captures
- Timeout headroom

`.config` · `Variable | Source | Type | Unit | Value` · 6 satir

- Latency samples
- Warmup triggers
- Capture timeout
- Sample interval
- Pulse width
- Backend memory

`.headroom` CSS bar grafigi · 3 bar

- Maximum observed
- Capture timeout
- Remaining headroom

Grafik: 1 SVG (kanonik geometri, bkz. S8)

---

## T15 — Non-blocking Spin vs Blocking DQBUF

| mod | status | bolumler |
| --- | --- | --- |
| Free-run | PASS | Measurement › Latency by capture mode › Capture mode evidence › CPU spin cost › Aggregate › Measurement Result › Test Configuration |
| Hardware-trigger | PASS | Measurement › Latency by capture mode › Capture mode evidence › CPU spin cost › Aggregate › Measurement Result › Test Configuration |
| Software-trigger | PASS | Measurement › Latency by capture mode › Capture mode evidence › CPU spin cost › Aggregate › Measurement Result › Test Configuration |

Alanlar (Free-run karti):

`.cmp` · `Metric | Type | Unit | Non-block | Block` · 5 satir

- Mean
- P95
- Maximum
- Minimum
- Standard deviation

`.spin` · `Metric | Type | Unit | Value | Detail` · 2 satir

- Average EAGAIN spins per frame
- Spin deadline

`.magg` · `Metric | Type | Unit | Value | Detail` · 2 satir

- Mean difference
- Lower mean mode

`.mres` · `Metric | Type | Unit | Value | Detail | Status` · 3 satir

- Non-block captures
- Blocking captures
- Modes compared

`.config` · `Variable | Source | Type | Unit | Value` · 5 satir

- Samples per mode
- Spin deadline
- Sample interval
- Warmup frames
- Backend memory

`.mode` CSS bar grafigi · 4 bar

- Non-block mean
- Block mean
- Non-block P95
- Block P95

---

## T16 — Trigger Pulse Width and Edge Detection

| mod | status | bolumler |
| --- | --- | --- |
| Free-run | SKIP | _(icerik yok)_ |
| Hardware-trigger | PASS | Measurement › Pulse width evidence › Edge evidence across the sweep › grafik › Edge evidence › Aggregate › Measurement Result › Test Configuration |
| Software-trigger | SKIP | _(icerik yok)_ |

Alanlar (Hardware-trigger karti):

`.pw` · `Width | Hits | HIGH mean | LOW mean (derived) | Detail` · 11 satir

  Per-item satir; ilk sutun `Width`.

`.edge` · `Metric | Type | Unit | Value | Detail` · 8 satir

- Edge result
- HIGH latency spread (cross-width)
- LOW latency spread (cross-width)
- Edge margin
- Deciding evidence
- Within-level range HIGH
- Within-level range LOW
- Within-level range basis

`.sum` · `Metric | Type | Unit | Value | Detail` · 2 satir

- Observed minimum tested
- Trigger edge

`.mres` · `Metric | Type | Unit | Value | Detail | Status` · 2 satir

- Sweep reliability
- Samples per width

`.config` · `Variable | Source | Type | Unit | Value` · 8 satir

- Pulse width levels
- Samples per width
- Total captures
- Poll timeout
- Warmup frames
- LOW edge reference
- Trigger edge
- Backend memory

Grafik: 1 SVG (kanonik geometri, bkz. S8)

---

## T17 — Pixel Format Performance Comparison

| mod | status | bolumler |
| --- | --- | --- |
| Free-run | WARN | Measurement › Measured format performance › Format evidence › Aggregate › Measurement Result › Test Configuration |
| Hardware-trigger | PASS | Measurement › Capture latency by pixel format › Memcpy throughput by pixel format › Format evidence › Aggregate › Measurement Result › Test Configuration |
| Software-trigger | PASS | Measurement › Capture latency by pixel format › Memcpy throughput by pixel format › Format evidence › Aggregate › Measurement Result › Test Configuration |

Alanlar (Hardware-trigger karti):

`.fmt` · `Format | Resolution | Sizeimage | Mean latency | Max latency | Memcpy throughput` · 2 satir

  Per-item satir; ilk sutun `Format`.

`.sum` · `Metric | Type | Unit | Value | Detail` · 4 satir

- Formats available
- Formats tested
- Latency difference
- Throughput difference

`.mres` · `Metric | Type | Unit | Value | Detail | Status` · 3 satir

- UYVY coverage
- NV16 coverage
- Formats tested

`.config` · `Variable | Source | Type | Unit | Value` · 7 satir

- Samples per format
- Memcpy repetitions
- Sizeimage
- Capture timeout
- Latency basis
- Throughput divisor
- Backend memory

`.bar` CSS bar grafigi · 6 bar

- UYVY mean
- NV16 mean
- UYVY max
- NV16 max
- UYVY
- NV16

---

## T18 — V4L2 Control Value Impact Analysis

| mod | status | bolumler |
| --- | --- | --- |
| Free-run | PASS | Measurement › Initial control snapshot › Value by value › Aggregate › Measurement Result › Test Configuration |
| Hardware-trigger | PASS | Measurement › Initial control snapshot › Value by value › Aggregate › Measurement Result › Test Configuration |
| Software-trigger | PASS | Measurement › Initial control snapshot › Value by value › Aggregate › Measurement Result › Test Configuration |

Alanlar (Free-run karti):

`.snap` · `Control | Current | Default | Access` · 6 satir

  Per-item satir; ilk sutun `Control`.

`.val` · `Control · test value | Capture | Latency | Unit | Detail` · 5 satir

  Per-item satir; ilk sutun `Control · test value`.

`.sum` · `Metric | Type | Unit | Value | Detail` · 6 satir

- Controls discovered
- Writable controls
- Read-only controls
- Values per control
- Captures per value
- Restore

`.mres` · `Metric | Type | Unit | Value | Detail | Status` · 4 satir

- HDR enable coverage
- Bypass Mode coverage
- Low Latency Mode coverage
- Write ISP format coverage

`.config` · `Variable | Source | Type | Unit | Value` · 7 satir

- Controls discovered
- Writable controls
- Captures per value
- Capture timeout
- Practical impact threshold
- Measured max difference
- Backend memory

---

## T19 — Resolution Capability and Performance

| mod | status | bolumler |
| --- | --- | --- |
| Free-run | PASS | Measurement › Capture performance at 1920x1280 › Resolution evidence › Aggregate › Measurement Result › Test Configuration |
| Hardware-trigger | PASS | Measurement › Capture performance at 1920x1280 › Resolution evidence › Aggregate › Measurement Result › Test Configuration |
| Software-trigger | PASS | Measurement › Capture performance at 1920x1280 › Resolution evidence › Aggregate › Measurement Result › Test Configuration |

Alanlar (Free-run karti):

`.res` · `Resolution | Pixel format | Mean latency | P95 latency | Throughput` · 1 satir

  Per-item satir; ilk sutun `Resolution`.

`.sum` · `Metric | Type | Unit | Value | Detail` · 4 satir

- Resolutions enumerated
- Resolutions measured
- Comparison pairs
- Pixel format

`.mres` · `Metric | Type | Unit | Value | Detail | Status` · 2 satir

- 1920x1280 coverage
- Resolutions measured

`.config` · `Variable | Source | Type | Unit | Value` · 6 satir

- Samples per resolution
- Resolutions enumerated
- Pixel format
- Capture timeout
- Latency basis
- Backend memory

`.bar` CSS bar grafigi · 3 bar

- Mean latency
- P95 latency
- Memcpy throughput

---

## T20 — Frame Sequence Continuity

| mod | status | bolumler |
| --- | --- | --- |
| Free-run | FAIL | Measurement › Sequence continuity › Continuity evidence › Aggregate › Measurement Result › Test Configuration |
| Hardware-trigger | PASS | Measurement › Sequence continuity › Continuity evidence › Aggregate › Measurement Result › Test Configuration |
| Software-trigger | PASS | Measurement › Sequence continuity › Continuity evidence › Aggregate › Measurement Result › Test Configuration |

Alanlar (Free-run karti):

`.cont` · `Metric | Type | Unit | Value | Detail` · 4 satir

- Requested / dequeued
- Sequence range
- Sequence span
- Largest single gap

`.sum` · `Metric | Type | Unit | Value | Detail` · 5 satir

- Frames dequeued
- Sequence gaps observed
- Unobserved sequences
- Duplicate sequences
- Backward events

`.mres` · `Metric | Type | Unit | Value | Detail | Status` · 3 satir

- Continuity requirement
- Duplicate sequences
- Backward sequences

`.config` · `Variable | Source | Type | Unit | Value` · 6 satir

- Requested frames
- Warmup frames
- Capture timeout
- Max allowed gaps
- Continuity signal
- Backend memory

---

## T21 — Buffer Timestamp Monotonicity

| mod | status | bolumler |
| --- | --- | --- |
| Free-run | PASS | Measurement › Sampled buffer timestamp delta › Delta evidence › Aggregate › Measurement Result › Test Configuration |
| Hardware-trigger | PASS | Measurement › Sampled buffer timestamp delta › Delta evidence › Aggregate › Measurement Result › Test Configuration |
| Software-trigger | PASS | Measurement › Sampled buffer timestamp delta › Delta evidence › Aggregate › Measurement Result › Test Configuration |

Alanlar (Free-run karti):

`.sup` · `Metric | Type | Unit | Value | Detail` · 3 satir

- Delta stddev
- Delta jitter
- Wall-buffer clock offset

`.sum` · `Metric | Type | Unit | Value | Detail` · 3 satir

- Buffer timestamps checked
- Non-monotonic events
- Timestamp source

`.mres` · `Metric | Type | Unit | Value | Detail | Status` · 1 satir

- Non-monotonic events

`.config` · `Variable | Source | Type | Unit | Value` · 6 satir

- Requested frames
- Warmup frames
- Capture timeout
- Max non-monotonic events
- Ordering signal
- Backend memory

`.delta` CSS bar grafigi · 3 bar

- Minimum
- Mean
- Max / P95

---

## T22 — Consecutive Frame Content Stability

| mod | status | bolumler |
| --- | --- | --- |
| Free-run | PASS | Measurement › Content comparison coverage › Comparison evidence › Measurement Result › Test Configuration |
| Hardware-trigger | PASS | Measurement › Content comparison coverage › Comparison evidence › Measurement Result › Test Configuration |
| Software-trigger | PASS | Measurement › Content comparison coverage › Comparison evidence › Measurement Result › Test Configuration |

Alanlar (Free-run karti):

`.sum` · `Metric | Type | Unit | Value | Detail` · 5 satir

- Frames compared
- Consecutive pairs
- Longest repeated run
- Comparison window
- Capture timeouts

`.mres` · `Metric | Type | Unit | Value | Detail | Status` · 1 satir

- Identical pairs

`.config` · `Variable | Source | Type | Unit | Value` · 6 satir

- Frames requested
- Pairs compared
- Compare bytes
- Identical threshold
- Capture timeout
- Backend memory

`.cov` CSS bar grafigi · 2 bar

- Unique pairs
- Identical pairs

---

## T23 — Sustained Capture Stability

| mod | status | bolumler |
| --- | --- | --- |
| Free-run | WARN | Measurement › Mean capture latency by window › grafik › Window evidence › Latency evidence › Aggregate › Measurement Result › Test Configuration |
| Hardware-trigger | PASS | Measurement › Mean capture latency by window › grafik › Window evidence › Latency evidence › Aggregate › Measurement Result › Test Configuration |
| Software-trigger | PASS | Measurement › Mean capture latency by window › grafik › Window evidence › Latency evidence › Aggregate › Measurement Result › Test Configuration |

Alanlar (Free-run karti):

`.win` · `Window | Captured | Mean | Stddev | Miss` · 6 satir

  Per-item satir; ilk sutun `Window`.

`.sum` · `Metric | Type | Unit | Value | Detail` · 3 satir

- Capture success
- Max consecutive miss
- Latency mean / P95

`.run` · `Metric | Type | Unit | Value | Detail` · 4 satir

- Observed test sampling rate
- Total capture misses
- Window detail coverage
- Windows reported

`.mres` · `Metric | Type | Unit | Value | Detail | Status` · 1 satir

- Latency drift

`.config` · `Variable | Source | Type | Unit | Value` · 7 satir

- Test duration
- Window size
- Sample interval
- Capture timeout
- Warmup frames
- Drift PASS limit
- Backend memory

Grafik: 1 SVG (kanonik geometri, bkz. S8)

---

## T24 — CPU Load Impact on Capture Latency

| mod | status | bolumler |
| --- | --- | --- |
| Free-run | PASS | Measurement › Capture latency by test phase › P95 impact against verdict thresholds › Statistic evidence › Phase evidence › Measurement validity › Measurement Result › Test Configuration |
| Hardware-trigger | PASS | Measurement › Capture latency by test phase › P95 impact against verdict thresholds › Statistic evidence › Phase evidence › Measurement validity › Measurement Result › Test Configuration |
| Software-trigger | PASS | Measurement › Capture latency by test phase › P95 impact against verdict thresholds › Statistic evidence › Phase evidence › Measurement validity › Measurement Result › Test Configuration |

Alanlar (Free-run karti):

`.ev` · `Statistic | Baseline | CPU load | Delta` · 6 satir

  Per-item satir; ilk sutun `Statistic`.

`.sum` · `Metric | Type | Unit | Value | Detail` · 3 satir

- Phase coverage
- Relative P95 change
- Mean delta

`.val` · `Metric | Type | Unit | Value | Detail` · 5 satir

- Configured load threads
- CPU saturation coverage
- Observed CPU utilization
- Baseline / load timeout
- Verdict scope

`.mres` · `Metric | Type | Unit | Value | Detail | Status` · 1 satir

- P95 delta

`.config` · `Variable | Source | Type | Unit | Value` · 7 satir

- Samples per phase
- Load threads
- Baseline timeout
- Load phase timeout
- P95 delta PASS limit
- P95 delta FAIL limit
- Backend memory

`.ph` CSS bar grafigi · 5 bar

- Baseline mean
- CPU load mean
- Baseline P95
- CPU load P95
- P95 delta

---

## T25 — Multi-camera Capture and Synchronization

| mod | status | bolumler |
| --- | --- | --- |
| Free-run | PASS | Measurement › Round capture coverage › Camera evidence › Round evidence › Measurement validity › Measurement method › Measurement Result › Test Configuration |
| Hardware-trigger | FAIL | Measurement › Round capture coverage › Camera evidence › Round evidence › Measurement validity › Measurement method › Measurement Result › Test Configuration |
| Software-trigger | PASS | Measurement › Round capture coverage › Camera evidence › Round evidence › Measurement validity › Measurement method › Measurement Result › Test Configuration |

Alanlar (Free-run karti):

`.cev` · `Camera | Role | Captures | Mean delivery | Max delivery | Sync samples` · 4 satir

  Per-item satir; ilk sutun `Camera`.

`.sum` · `Metric | Type | Unit | Value | Detail` · 3 satir

- Participants
- Per-camera coverage
- Acquisition skew P95

`.mval` · `Metric | Type | Unit | Value | Detail` · 5 satir

- Capture round alignment
- Common physical trigger
- Buffer timestamp comparison
- DQBUF receipt timing
- Synchronization verdict

`.mmet` · `Metric | Type | Unit | Value | Detail` · 3 satir

- Trigger source
- Timing reference
- Sync measurement

`.mres` · `Metric | Type | Unit | Value | Detail | Status` · 1 satir

- Complete rounds

`.config` · `Variable | Source | Type | Unit | Value` · 8 satir

- Requested rounds
- Participants
- Round deadline
- Capture PASS limit
- Capture FAIL limit
- Sync PASS limit
- Sync FAIL limit
- Backend memory

`.cam` CSS bar grafigi · 5 bar

- /dev/video4
- /dev/video5
- /dev/video6
- /dev/video7
- All cameras

---

## T26 — Post-STREAMON Latency Stabilization

| mod | status | bolumler |
| --- | --- | --- |
| Free-run | WARN | Measurement › Warm-up outcome by fresh session › grafik › Cycle evidence › Aggregate › Measurement validity › Measurement method › Measurement Result › Test Configuration |
| Hardware-trigger | PASS | Measurement › Warm-up outcome by fresh session › grafik › Cycle evidence › Aggregate › Measurement validity › Measurement method › Measurement Result › Test Configuration |
| Software-trigger | PASS | Measurement › Warm-up outcome by fresh session › grafik › Cycle evidence › Aggregate › Measurement validity › Measurement method › Measurement Result › Test Configuration |

Alanlar (Free-run karti):

`.cev` · `Cycle | Session | Warm-up outcome | Detail` · 10 satir

- C1
- C2
- C3
- C4
- C5
- C6
- C7
- C8
- C9
- C10

`.sum` · `Metric | Type | Unit | Value | Detail` · 3 satir

- Stabilized cycles
- Longest warm-up
- Median warm-up of stabilized cycles

`.mval` · `Metric | Type | Unit | Value | Detail` · 5 satir

- Fresh V4L2 sessions
- Stabilized within window
- Frame misses recorded
- Reference window
- Free-run timing source

`.mmet` · `Metric | Type | Unit | Value | Detail` · 4 satir

- Fresh session
- Maximum observation window
- Steady-state reference
- Latency tolerance

`.mres` · `Metric | Type | Unit | Value | Detail | Status` · 2 satir

- Stabilized cycles
- Fresh sessions

`.config` · `Variable | Source | Type | Unit | Value` · 6 satir

- Fresh cycles
- Observation window (frames)
- Reference window
- Latency tolerance
- Capture timeout
- Backend memory

Grafik: 1 SVG (kanonik geometri, bkz. S8)

---
