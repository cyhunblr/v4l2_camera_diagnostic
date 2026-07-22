<p align="center">
  <img alt="V4L2 Camera Diagnostic logo" src="assets/v4l2_camera_diagnostic_logo.png" width="300">
</p>

<h1 align="center">V4L2 Camera Diagnostic</h1>

<p align="center">
  <strong>A local web application for diagnosing V4L2 cameras, trigger modes, and Linux camera memory backends.</strong>
</p>

<p align="center">
  <a href="https://github.com/cyhunblr/v4l2_camera_diagnostic/actions/workflows/ci.yml"><img alt="CI" src="https://github.com/cyhunblr/v4l2_camera_diagnostic/actions/workflows/ci.yml/badge.svg"></a>
  <a href="CMakeLists.txt"><img alt="Build system" src="https://img.shields.io/badge/build-CMake-064F8C?logo=cmake&logoColor=white"></a>
  <img alt="C++ standard" src="https://img.shields.io/badge/C%2B%2B-17-00599C?logo=cplusplus&logoColor=white">
  <img alt="Platform" src="https://img.shields.io/badge/platform-Linux-FCC624?logo=linux&logoColor=black">
  <a href="LICENSE"><img alt="License" src="https://img.shields.io/badge/license-MPL--2.0-blue"></a>
</p>

## Overview

`v4l2-camera-diagnostic` is a local web application for repeatable camera
validation on Linux: device discovery, configurable trigger profiles, memory
backend checks, structured test runs, and JSON/Markdown/HTML/PDF reports.
Most day-to-day workflows can be run from the browser, while local profiles
and developer workflows remain scriptable from the CLI.

## Quick Start

```bash
git clone https://github.com/cyhunblr/v4l2_camera_diagnostic.git
cd v4l2_camera_diagnostic
./installation.sh
```

Then launch it:

```bash
v4l2-camera-diagnostic-web
```

This starts a local server and opens your browser automatically at
`http://127.0.0.1:8765`. The installer also writes a desktop launcher under
`~/.local/share/applications`; desktop environments that index that directory
can show **V4L2 Camera Diagnostic** in the application menu.

From the browser you can discover cameras, select hardware, V4L2 software, or
free-run triggering, route cameras to profile channels, choose tests and
memory backends, and watch live progress. See
[`docs/frontend/web-ui.md`](docs/frontend/web-ui.md) for the full walkthrough.

Full installer behavior (what gets installed, where, and what dependencies
it checks for) is documented in
[`docs/guides/installation.md`](docs/guides/installation.md).

## Reports

Every run produces structured report artifacts — the browser view is not
the canonical result. The web UI can generate:

- JSON for automation
- Markdown for review and issue attachments
- HTML for local human-readable reports

HTML reports include browser PDF export. The CLI also accepts `--report pdf`
for environments that want a PDF artifact generated during the run.

See [`docs/guides/reports.md`](docs/guides/reports.md).

## Uninstalling

```bash
./uninstallation.sh
```

Interactively asks whether to remove user data and local build artifacts.
Generated reports are never deleted by this script, regardless of flags —
see [`docs/guides/uninstallation.md`](docs/guides/uninstallation.md) for
every flag and exactly what each one removes.

## Documentation

**Using the application:**

- [`docs/frontend/web-ui.md`](docs/frontend/web-ui.md) — the web UI, its runtime model, and LAN access
- [`docs/guides/reports.md`](docs/guides/reports.md) — report formats and contents
- [`docs/guides/configuration.md`](docs/guides/configuration.md) — camera, backend, and profile concepts
- [`docs/guides/installation.md`](docs/guides/installation.md) — what the installer does
- [`docs/guides/uninstallation.md`](docs/guides/uninstallation.md) — what the uninstaller removes and preserves

**Project internals** (for contributors and integrators - run
`scripts/setup-dev-env.sh` once for local CI-style tooling and hooks, then see
[`CONTRIBUTING.md`](CONTRIBUTING.md)):

- [`docs/architecture.md`](docs/architecture.md) — directory layout and subsystem overview
- [`docs/roadmap.md`](docs/roadmap.md) — current roadmap and remaining gaps
- [`docs/testing.md`](docs/testing.md) — unit tests, static analysis, and the diagnostic-test inventory (each links to a detailed per-test page under [`docs/backend/tests/`](docs/backend/tests/))
- [`docs/ci.md`](docs/ci.md) — GitHub Actions pipeline
- [`docs/development/cli.md`](docs/development/cli.md) — the developer CLI, including custom profile management
- [`docs/backend/hardware-validation.md`](docs/backend/hardware-validation.md) — hardware validation checklist

## License

This project is licensed under the Mozilla Public License 2.0. See [`LICENSE`](LICENSE).
