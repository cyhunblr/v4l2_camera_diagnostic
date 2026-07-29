# Installation

`./installation.sh` is the only supported way to install
`v4l2-camera-diagnostic` for end-user (non-development) use. This document
describes exactly what it does, in order, so its behavior never needs to be
guessed from the script source.

## Usage

```bash
./installation.sh [--dry-run] [--debug]
```

`--dry-run` previews the install without executing file-changing commands.
`--debug` prints each command (prefixed with `+`) and lets full command output
flow through the terminal. Without `--debug`, the installer prints compact
step-based progress with a spinner for active work and expands captured
command output only when a command fails.

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

5. **Enables kernel log access** by adding you to the `adm` group
   (`sudo usermod -aG adm $(id -un)`), if you are not already in `adm` or
   `systemd-journal`. The web app's **Export DMESG** button reads the kernel
   log with `journalctl -k -b`, and the journal directories carry an ACL that
   grants those groups read access — so the app itself needs no elevated
   privileges at run time.

   This step is skipped when you already have access, and you are asked
   before it happens. Group membership only takes effect on your **next
   login**, so Export DMESG will not work in the current session until you
   log out and back in. To do it yourself later:

   ```bash
   sudo usermod -aG adm "$(id -un)"   # then log out and back in
   ```

6. **Checks `PATH`** and prints a one-line notice if `~/.local/bin` is not
   already on it.

### About the sudo password

The installer asks the questions first (install missing dependencies? join
`adm`?), then requests the sudo password **once**, and only if at least one
answer actually requires it. On a machine whose dependencies are already
present and whose user is already in `adm`, the installer never asks for a
password at all.

The credential is dropped with `sudo -k` when the script exits — including
on failure — so it does not stay valid in your terminal afterwards.

## Launch

From a terminal:

```bash
v4l2-camera-diagnostic-web
```

This starts a local server bound to `127.0.0.1` and opens the default browser.
For access from another trusted device on the same LAN, bind the server to all
interfaces and open the device's LAN IP from the other browser:

```bash
v4l2-camera-diagnostic-web --host 0.0.0.0 --port 8765
```

See [`docs/frontend/web-ui.md`](../frontend/web-ui.md#lan-access) for the LAN
access notes.

## Uninstall

See [`docs/guides/uninstallation.md`](uninstallation.md).
