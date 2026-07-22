# Testing

This project has three independent layers of testing: backend unit tests,
frontend type-checking, and physical-camera diagnostics. Each layer has a
different scope, a different local command, and a different place in CI (see
[`docs/ci.md`](ci.md)).

## Backend Unit Tests

Unit tests cover pure logic that does not require a camera or GPIO hardware:
statistics computation, profile registry parsing, and report generation.

Build and run them with CTest:

```bash
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build --output-on-failure
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

## Frontend Checks

The web UI (`source/frontend`) has no runtime test suite yet — the
type checker is the primary safety net:

```bash
cd source/frontend
npm ci
npm run build   # tsc --noEmit && vite build
npm run lint    # eslint .
```

Adding a real component/unit test suite (e.g. Vitest) is tracked as future
work; until then, `npm run build` and `npm run lint` are the required checks.

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

- `pre-commit` runs before a commit is created and checks all backend C++
  sources with the same `clang-format-18` dry-run used by CI
- `commit-msg` runs while creating a commit and checks the Conventional Commit
  type, lower-case type, non-empty subject, and 100-character header limit from
  `commitlint.config.cjs`
- `pre-push` runs before objects are sent to the remote and repeats the C++
  formatting check so skipped commit hooks are caught before push

The pre-commit hook intentionally requires the `clang-format-18` command, the
same binary name CI runs. Install it with:

```bash
sudo apt-get install -y clang-format-18
```

You can run the shared checks manually before committing or pushing:

```bash
scripts/dev/check-cpp-format.sh
.githooks/pre-commit
.githooks/pre-push
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

The modular runner contains 24 implemented diagnostics. Each ID in the
inventory below links to a detailed reference page under
[`docs/backend/tests/`](backend/tests/) covering scope, inputs, output
metrics, interpretation guidance, and a walkthrough of the implementation.

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

| ID | Name | Category | Notes |
| --- | --- | --- | --- |
| [t01-no-streamon](backend/tests/t01-no-streamon.md) | Frame capture without STREAMON | stream-state | |
| [t02-buffer-overwrite](backend/tests/t02-buffer-overwrite.md) | Buffer overwrite behavior | buffering | risky; no free-run (Hardware/Software only) |
| [t03-gpio-latency](backend/tests/t03-gpio-latency.md) | Trigger to DQBUF latency | latency | no free-run (Hardware/Software only) |
| [t04-nonblock-vs-block](backend/tests/t04-nonblock-vs-block.md) | NON_BLOCK vs BLOCK comparison | io-mode | |
| [t05-poll-timeout-effect](backend/tests/t05-poll-timeout-effect.md) | Poll timeout effect | polling | |
| [t06-format-comparison](backend/tests/t06-format-comparison.md) | Format comparison | format | |
| [t07-poll-timeout-sweep](backend/tests/t07-poll-timeout-sweep.md) | Poll timeout sweep | polling | |
| [t08-sequence-continuity](backend/tests/t08-sequence-continuity.md) | Sequence number continuity | sequence | |
| [t09-sustained-capture](backend/tests/t09-sustained-capture.md) | Sustained capture stability | stability | long-running |
| [t10-multi-buffer](backend/tests/t10-multi-buffer.md) | Multi-buffer configurations | buffering | |
| [t11-buffer-recycling](backend/tests/t11-buffer-recycling.md) | Buffer recycling timing | buffering | |
| [t12-stream-cycles](backend/tests/t12-stream-cycles.md) | STREAMON/STREAMOFF cycles | stream-state | |
| [t13-buffer-flags](backend/tests/t13-buffer-flags.md) | V4L2 buffer flag analysis | metadata | |
| [t14-timestamp-monotonicity](backend/tests/t14-timestamp-monotonicity.md) | Timestamp monotonicity | metadata | |
| [t15-memory-throughput](backend/tests/t15-memory-throughput.md) | Memory access throughput | memory | benchmarks the selected backend's device-mapped buffer |
| [t16-v4l2-compliance](backend/tests/t16-v4l2-compliance.md) | V4L2 compliance checks | v4l2 | no hardware trigger required |
| [t17-pollerr-handling](backend/tests/t17-pollerr-handling.md) | POLLERR/POLLHUP handling | stream-state | experimental, risky |
| [t18-dmabuf-cache-sync](backend/tests/t18-dmabuf-cache-sync.md) | DMA_BUF_IOCTL_SYNC cache coherency | dmabuf | **requires the DMABUF backend** — skipped for `mmap`/`userptr` runs by design (`requires_dmabuf`), not a bug |
| [t19-gpio-pulse-width](backend/tests/t19-gpio-pulse-width.md) | GPIO pulse width characterization | trigger | Hardware trigger only |
| [t20-camera-controls](backend/tests/t20-camera-controls.md) | Camera control parameter effect | controls | experimental, risky, profile-scoped (ISX021) |
| [t21-stuck-frame](backend/tests/t21-stuck-frame.md) | Stuck frame detection | quality | |
| [t22-latency-under-load](backend/tests/t22-latency-under-load.md) | Latency under CPU load | stability | |
| [t24-control-inventory](backend/tests/t24-control-inventory.md) | V4L2 control inventory | controls | no active trigger required |
| [v4l2-memory-probe](backend/tests/v4l2-memory-probe.md) | V4L2 memory backend probe | v4l2 | no hardware trigger required |

Every test above has a real implementation in `diagnostic_runner.cpp` — none
return a placeholder "pending migration" skip. A test being reported as
`Skipped` at runtime is always one of the following expected conditions in
`run_test()`, never a missing implementation:

- **Memory-backend skip** — `t18`'s DMABUF requirement (`requires_dmabuf`):
  selecting `mmap` or `userptr` will correctly show `t18` as `Skipped` with
  "Test requires DMABUF..." — `t18` is the one and only test with this
  memory-backend condition, and it is expected behavior, not a defect in the
  `mmap`/`userptr` path.
- **Trigger-mode skip** — tests whose `trigger_mode_mask` does not include the
  selected run mode (checked by `supports_trigger_mode()`) are skipped
  explicitly. Pulse-width characterization (`t19`) is hardware-only;
  buffer-overwrite (`t02`) and trigger-latency (`t03`) require an active
  hardware or software trigger and are skipped in free-run mode.
- **Trigger-source skip** — an active-mode test is skipped when its profile,
  channel, GPIO, control device, or V4L2 control validation is unavailable.
- **Missing `linux/dma-buf.h`** — `t18` additionally compiles to a `Skipped`
  result ("linux/dma-buf.h not available...") on systems without the DMA-BUF
  sync header.

The fallback "No core implementation registered for this test." skip is only
reachable for an unknown test id, not for any of the 24 built-in tests above.

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
