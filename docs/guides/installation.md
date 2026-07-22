# Installation

`./installation.sh` is the only supported way to install
`v4l2-camera-diagnostic` for end-user (non-development) use. This document
describes exactly what it does, in order, so its behavior never needs to be
guessed from the script source.

## Usage

```bash
./installation.sh [--dry-run]
```

`--dry-run` prints every command the script would run (prefixed with `+`)
without executing it — use it to preview the install on an unfamiliar
machine.

## What it does

1. **Checks dependencies.** Looks for `cmake`, `pkg-config`, `xdg-open`, and
   the `libmicrohttpd`, `jsoncpp`, and `libgpiod` pkg-config modules. If
   anything is missing and `apt-get` is available, it installs:

   | Package | Why |
   | --- | --- |
   | `build-essential` | C++ compiler and linker toolchain |
   | `cmake` | build system |
   | `pkg-config` | locates the three libraries below |
   | `libgpiod-dev` | GPIO hardware-trigger control |
   | `libmicrohttpd-dev` | the web UI's embedded HTTP server |
   | `libjsoncpp-dev` | JSON (used by the HTTP API and JSON reports) |
   | `xdg-utils` | opens the default browser when the web app starts |

   On a non-Debian system without `apt-get`, the script prints this package
   list and expects them to be installed manually before continuing.

2. **Builds the web UI** (`source/frontend`), if `npm`-buildable: ensures a
   working Node.js via `nvm` (installing `nvm` and Node 18 if the system
   Node is older than 12), then runs `npm ci` (or `npm install` if no
   lockfile exists) and `npm run build`.

3. **Builds the C++ project** with `cmake -S . -B build` and
   `cmake --build build --parallel` — the same build every executable in
   this repository comes from.

4. **Installs files** under `~/.local`:

   | Installed path | Source |
   | --- | --- |
   | `~/.local/bin/v4l2-camera-diagnostic` | CLI binary (developer/advanced use — see [`docs/development/cli.md`](../development/cli.md)) |
   | `~/.local/bin/v4l2-camera-diagnostic-web` | the web application — this is what end users run |
   | `~/.local/share/v4l2-camera-diagnostic/web` | built web UI assets |
   | `~/.local/share/v4l2-camera-diagnostic/docs` | a copy of this `docs/` tree |
   | `~/.local/share/applications/v4l2-camera-diagnostic.desktop` | desktop launcher for environments that index user applications |

5. **Checks `PATH`** and prints a one-line notice if `~/.local/bin` is not
   already on it.

## Launch

From a terminal:

```bash
v4l2-camera-diagnostic-web
```

The installer also writes a desktop launcher to
`~/.local/share/applications/v4l2-camera-diagnostic.desktop`. Desktop
environments that index user applications may show **V4L2 Camera Diagnostic**
in the application menu. The launcher points at the same binary, so either
entry point starts a local server and opens the default browser.

## Uninstall

See [`docs/guides/uninstallation.md`](uninstallation.md).
