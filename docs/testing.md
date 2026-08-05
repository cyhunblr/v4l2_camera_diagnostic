# Testing

This project has three independent layers of testing: backend unit tests,
web UI tests, and physical-camera diagnostics. Each layer has a different
scope, a different local command, and a different place in CI (see
[`docs/ci.md`](ci.md)).

## Backend Unit Tests

Unit tests cover pure logic that does not require a camera or GPIO hardware:
statistics computation, profile registry parsing, and report generation.

Build and run them with CTest:

```bash
cmake -S . -B build
cmake --build build --parallel
(cd build && ctest --output-on-failure)
```

| Test binary | Source | Covers |
| --- | --- | --- |
| `v4l2diag_stats_test` | `tests/stats_test.cpp` | `compute_stats()` — mean, stddev, percentiles, jitter, outliers |
| `v4l2diag_profile_registry_test` | `tests/profile_registry_test.cpp` | JSON round-trip, validation, add/remove |
| `v4l2diag_trigger_model_test` | `tests/trigger_model_test.cpp` | Trigger modes, profile validation, mode compatibility |
| `v4l2diag_report_writer_test` | `tests/report_writer_test.cpp` | JSON/Markdown/HTML report generation from a `RunResult` |

These tests link against `v4l2diag_core` and run on any Linux machine —
no camera, no GPIO chip, no root access required. This is why they run on
every push and pull request in hosted CI.

### Strict warnings

Configure with `-DV4L2DIAG_ENABLE_WARNINGS_AS_ERRORS=ON` to fail the build on
any compiler warning (`-Wall -Wextra -Wpedantic -Werror`). CI always builds
with this flag; run it locally before opening a pull request:

```bash
cmake -S . -B build -DV4L2DIAG_ENABLE_WARNINGS_AS_ERRORS=ON
cmake --build build --parallel
```

## Web UI Tests

The web UI (`source/frontend`) is tested with [Vitest](https://vitest.dev) and
[React Testing Library](https://testing-library.com/react) in a `jsdom`
environment. Tests live next to the code they cover as `*.test.tsx`.

**Node 22 is required** — the project standard, matching CI and
`installation.sh`.

```bash
cd source/frontend
npm ci
npm test            # vitest run
npm run test:watch  # vitest, re-runs on change
npm run test:coverage
npm run build       # tsc --noEmit && vite build
npm run lint        # eslint .
```

Run `npm run lint` after `npm run test:coverage` if you like — `coverage/` is
in both `.gitignore` and the ESLint ignore list, so it never shows up as
lint noise.

Vitest is configured inside [`vite.config.ts`](../source/frontend/vite.config.ts)
rather than a separate config file, so the app and the tests share one plugin
setup. Global setup (jest-dom matchers, per-test cleanup) lives in
[`src/test/setup.ts`](../source/frontend/src/test/setup.ts).

What the current suite covers:

| Area | Locked behaviour |
| --- | --- |
| `SelectableCard` | `aria-pressed`/`aria-disabled`, Enter/Space activation, disabled cards stay focusable but do not toggle, corner action is not nested in the selection button and never toggles the card |
| `ResultsTable` | empty state, row order, status labels, `status-*` row class, leading `✓` stripped, summaries sharing a message are not de-duplicated |
| `Sidebar` | sidebar nav and the mobile `<select>` offer the same destinations, Configure/Output grouping, locked pages disabled in both surfaces with the same reason, Start/Stop swap |

All four commands must pass before pushing; `npm run lint` must report zero
warnings.

## Local Git Hooks

For contributor machines, run the full developer setup once after cloning:

```bash
scripts/setup-dev-env.sh
```

This installs local developer tools where possible, installs frontend
dependencies with `npm ci`, and enables the repository Git hooks. If your
machine already has the tools and you only want to enable hooks for this clone,
run:

```bash
scripts/install-git-hooks.sh
```

Git ignores versioned hook files until each developer opts in. The hook-only
installer records this local-only Git setting:

```bash
git config --local core.hooksPath .githooks
```

After installation, the hooks run automatically during normal Git commands:

- `pre-commit` checks **only staged files** with the same tools CI uses:
  `clang-format-18`, `cpplint`, `markdownlint`, `eslint`, and `shellcheck`.
  Unstaged changes do not block a commit.
- `commit-msg` checks the Conventional Commit type, lower-case type, non-empty
  subject, and 100-character header limit from `commitlint.config.cjs`
- `pre-push` builds the project with warnings-as-errors and runs the test suite
  (`cmake --build build-strict && ctest`). This is the last gate before code
  reaches the remote.

The pre-commit hook requires `clang-format-18`, `cpplint`, `markdownlint`,
`shellcheck`, and (for frontend changes) `npm`/`npx`. Install with:

```bash
sudo apt-get install -y clang-format-18 shellcheck
pip install --user cpplint
npm install -g markdownlint-cli
```

You can run the shared checks manually before committing or pushing:

```bash
scripts/dev/check-cpp-format.sh
scripts/dev/check-cpplint.sh
scripts/dev/check-md-lint.sh
scripts/dev/check-frontend-lint.sh
scripts/dev/check-shellcheck.sh
.githooks/commit-msg .git/COMMIT_EDITMSG
```

If formatting fails, fix it with the command printed by the hook, or run:

```bash
find source/backend \( -name "*.cpp" -o -name "*.hpp" \) -print0 | xargs -0 clang-format-18 -i
```

## Static Analysis

| Tool | Scope | Config file | Local command |
| --- | --- | --- | --- |
| `cpplint` | `source/backend/**` | `CPPLINT.cfg` | `cpplint --recursive source/backend/` |
| `clang-format` | `source/backend/**` | `.clang-format` | `find source/backend -name "*.cpp" -o -name "*.hpp" \| xargs clang-format-18 --dry-run --Werror` |
| `eslint` | `source/frontend/**` | `source/frontend/eslint.config.js` | `cd source/frontend && npm run lint` |
| `markdownlint` | `**/*.md` | `.markdownlint.json`, `.markdownlintignore` | `markdownlint '**/*.md'` |
| `commitlint` | commit messages | `commitlint.config.cjs` | `.githooks/commit-msg` locally, enforced in CI |

`CPPLINT.cfg` deliberately disables a handful of Google-style rules
(`legal/copyright`, `build/include_order`, `runtime/int`, `build/namespaces`,
`readability/braces`, `whitespace/newline`, `runtime/references`,
`whitespace/indent_namespace`) that conflict with established, intentional
patterns in this codebase (per-file copyright headers are not used — the
top-level `LICENSE` covers the MPL-2.0 grant — out-parameters passed by
reference, a CLI-ergonomics `using namespace`, and a libmicrohttpd-matching
`unsigned short` port type). These are scoping choices, not oversights —
revisit them only alongside an explicit decision to change the underlying
code pattern.

CI pins **`clang-format-18`** specifically (`sudo apt-get install -y
clang-format-18`) rather than the distro-default `clang-format` package,
because different major versions format complex ternary/conditional
expressions differently — a mismatch between your local version and CI's
will produce false failures that look like a code problem but are actually
a tooling version mismatch. Install `clang-format-18` locally
(`sudo apt-get install -y clang-format-18`, available via the `universe`
component on Ubuntu) to get results that exactly match CI.
Use `scripts/setup-dev-env.sh` for contributor tooling; it requires the same
`clang-format-18` command CI uses.

## Camera Diagnostic Tests

The modular runner contains 26 registered diagnostics, all fully implemented.
Each ID in the inventory below links to a detailed
reference page under [`docs/backend/tests/`](backend/tests/) covering scope,
inputs, output metrics, interpretation guidance, and a walkthrough of the
implementation.

### Trigger modes

Most tests fire a *trigger* to make the sensor produce a frame, then measure
what comes back. The runner supports three modes, selected per run
(`trigger_mode_mask` in
[test_registry.cpp](../source/backend/core/src/test_registry.cpp)):

| Mode | Bit | Source | What it does |
| --- | --- | --- | --- |
| `Hardware` | `0x01` | `GpioTrigger` | Drives a physical GPIO line wired to the sensor's external-trigger input. |
| `Software` | `0x02` | `V4l2ControlTrigger` | Fires the sensor through a V4L2 control write — no GPIO wiring required. |
| `FreeRun` | `0x04` | `FreeRunTrigger` | No trigger at all; the sensor streams on its own clock. |

`supports_trigger_mode()` checks a mode's bit against the mask; running a test
in a mode it does not support reports `Skipped` (expected, not a failure).
Unless a row's Notes below say otherwise, a test supports all three modes.
Hardware and software triggering require suitable local profiles and device
permissions; free-run requires only a streaming camera. Hosted CI cannot
provide these devices, so use the checklist in [`docs/ci.md`](ci.md) and the
hardware validation checklist in
[`docs/backend/hardware-validation.md`](backend/hardware-validation.md).

### Test inventory

Tests are grouped into 7 logical layers that run in dependency order:

#### Layer 1 — Discovery

| ID | Name | Category | Tags | Notes |
| --- | --- | --- | --- | --- |
| t01-device-compliance | V4L2 device compliance | discovery | `stable` | no trigger required; includes memory backend probe |
| t02-control-inventory | V4L2 control inventory | discovery | `stable` | no trigger required |

#### Layer 2 — State-machine correctness

| ID | Name | Category | Tags | Notes |
| --- | --- | --- | --- | --- |
| t03-pipeline-ready | Pipeline readiness after STREAMON | stream-state | `stable` | times the first frame without `poll()`; also reports how long STREAMON itself takes |
| t04-no-streamon | Frame capture without STREAMON | stream-state | `stable` | |
| t05-pollerr-handling | POLLERR/POLLHUP handling | stream-state | `stress` | checks DQBUF rejection after STREAMOFF |
| t06-stream-cycles | STREAMON/STREAMOFF cycle reliability | stream-state | `stress` | rapid stream setup/teardown cycles |

#### Layer 3 — Buffer & memory

| ID | Name | Category | Tags | Notes |
| --- | --- | --- | --- | --- |
| t07-multi-buffer | Multi-buffer configurations | buffering | `stable` | |
| t08-buffer-overwrite | Buffer overwrite behavior | buffering | `stress` | no free-run (Hardware/Software only) |
| t09-buffer-recycling | Buffer recycling timing | buffering | `stable` | |
| t10-buffer-flags | V4L2 buffer flag analysis | metadata | `stable` | |
| t11-memory-throughput | Memory access throughput | memory | `benchmark` | no trigger required |
| t12-dmabuf-cache-sync | DMA_BUF_IOCTL_SYNC cache coherency | dmabuf | `device-specific` | **requires DMABUF backend** |

#### Layer 4 — Polling / timeout

| ID | Name | Category | Tags | Notes |
| --- | --- | --- | --- | --- |
| t13-poll-timeout-cliff | Poll timeout cliff finder | polling | `stable` | adaptive binary search + stability tracking |

#### Layer 5 — Latency

| ID | Name | Category | Tags | Notes |
| --- | --- | --- | --- | --- |
| t14-trigger-latency | Trigger to DQBUF latency | latency | `benchmark` | no free-run (Hardware/Software only) |
| t15-nonblock-vs-block | NON_BLOCK vs BLOCK comparison | io-mode | `device-specific` | |
| t16-gpio-pulse-width | GPIO pulse width characterization | trigger | `device-specific` | Hardware trigger only |
| t17-format-comparison | Format comparison | format | `benchmark` | |
| t18-control-sweep | Control parameter sweep | controls | `stress`, `benchmark` | sweeps writable V4L2 controls |
| t19-resolution-sweep | Resolution sweep | format | `benchmark` | sweeps supported resolutions |

#### Layer 6 — Integrity

| ID | Name | Category | Tags | Notes |
| --- | --- | --- | --- | --- |
| t20-sequence-continuity | Sequence number continuity | sequence | `stable` | |
| t21-timestamp-monotonicity | Timestamp monotonicity | metadata | `stable` | |
| t22-stuck-frame | Stuck frame detection | quality | `stable` | |

#### Layer 7 — Stability

| ID | Name | Category | Tags | Notes |
| --- | --- | --- | --- | --- |
| t23-sustained-capture | Sustained capture stability | stability | `long-running` | long-running capture session |
| t24-latency-under-load | Latency under CPU load | stability | `benchmark` | latency while CPU cores saturated |
| t25-multi-camera | Multi-camera contention | stability | `long-running` | cross-device jitter under concurrent capture |
| t26-cold-start | Cold-start warm-up cost | stability | `stable` | frames to steady-state |

Tests marked **not yet implemented** report `Skipped` at runtime.
A test being reported as `Skipped` is always one of the following expected
conditions in `run_test()`, never a missing implementation for an implemented
test:

- **Memory-backend skip** — `t12`'s DMABUF requirement (`requires_dmabuf`):
  selecting `mmap` or `userptr` will correctly show `t12` as `Skipped` with
  "Test requires DMABUF..." — `t12` is the one and only test with this
  memory-backend condition, and it is expected behavior, not a defect in the
  `mmap`/`userptr` path.
- **Trigger-mode skip** — tests whose `trigger_mode_mask` does not include the
  selected run mode (checked by `supports_trigger_mode()`) are skipped
  explicitly. Pulse-width characterization (`t16`) is hardware-only;
  buffer-overwrite (`t08`) and trigger-latency (`t14`) require an active
  hardware or software trigger and are skipped in free-run mode.
- **Trigger-source skip** — an active-mode test is skipped when its profile,
  channel, GPIO, control device, or V4L2 control validation is unavailable.
- **Missing `linux/dma-buf.h`** — `t12` additionally compiles to a `Skipped`
  result ("linux/dma-buf.h not available...") on systems without the DMA-BUF
  sync header.

List everything the current binary knows about, with implementation status:

```bash
v4l2-camera-diagnostic tests list --all
```

### Interpretation thresholds (Phase 0)

Pass/Warn/Fail cut-offs quoted on the per-test pages are **documentation
defaults**, not values the runner currently enforces — today most tests pass
as long as they collect data, and interpretation is left to the reader. Making
these thresholds configurable (a defaults file plus a web UI to override them
per system) is planned; see [`docs/roadmap.md`](roadmap.md). Until then, treat
the numbers as starting points to calibrate against your own sensor and frame
rate.

### Reference pages

Each page under [`docs/backend/tests/`](backend/tests/) follows the same five
sections — Overview, Trigger modes, Inputs, Outputs & interpretation, and How
the code works — and links to its `run_tXX` implementation in
[`diagnostic_runner.cpp`](../source/backend/core/src/diagnostic_runner.cpp)
(search the function name, no parentheses). Each `run_tXX` in turn carries a
`// Docs:` comment pointing back to its page, so the two stay navigable in both
directions.

### Refactor Notes

- Move repeated capture, drain, warmup, and polling logic into shared fixtures.
- Replace terminal-first output with structured `TestResult` data.
- Use monotonic clocks for latency measurements.
- Make frame size, format, timeout, pulse width, sample count, and camera
  controls configurable.
- Scope camera-specific control sweeps to profiles.
- Run DMABUF-only tests only when DMABUF is selected and supported.
