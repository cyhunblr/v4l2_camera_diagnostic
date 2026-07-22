# Web UI

The web UI is the primary human-facing experience for `v4l2-camera-diagnostic`.

## Runtime Model

`v4l2-camera-diagnostic-web` starts a local C++ HTTP server bound to `127.0.0.1`, serves the web UI, and opens the browser automatically.

Default URL:

```text
http://127.0.0.1:8765
```

If the port is busy, the launcher tries the next available port.

## LAN Access

Remote LAN access is disabled by default. To access the web UI from another trusted device on the same network, bind the server to all interfaces:

```bash
v4l2-camera-diagnostic-web --host 0.0.0.0 --port 8765
```

Then open the target device LAN address from the other machine:

```text
http://<device-lan-ip>:8765
```

Only use this on a trusted network. The web UI can start diagnostics, access local V4L2 devices, and expose generated reports.

## Report Storage

Generated reports and the Dashboard's run history (`runs-index.json`) are written under a stable, absolute directory — not the process's current working directory — so they persist across launches regardless of how the app is started (desktop icon, terminal, different working directory):

```text
$XDG_DATA_HOME/v4l2-camera-diagnostic/reports
```

or, if `XDG_DATA_HOME` is unset:

```text
$HOME/.local/share/v4l2-camera-diagnostic/reports
```

Use `--report-root DIR` to override this location. `uninstallation.sh` never deletes this directory (including with `--purge`), so reports and run history survive an uninstall/reinstall cycle.

## Main Features

- discovered camera list
- hardware, software, and free-run trigger modes
- visual camera-to-channel routing
- local profile creation, update, and removal
- memory backend selection
- test selection
- report format selection
- run progress
- Live Output panel
- report download links

## Profile Management

Profiles are local machine configuration. The Profiles view can create and
remove profiles, discover controls on video and subdevice nodes, and save
camera-to-channel routing as profile defaults.

Every run selects exactly one trigger mode. In hardware or software mode,
camera nodes on the left connect to compatible profile channels on the right.
Use **Single profile** to apply one rig profile to every selected camera, or
**Per camera** to route cameras across different profiles. Free-run mode does
not require routing.

## Live Output

The Live Output panel shows runtime log lines from diagnostic runs. It supports:

- info, warning, and error severity labels
- severity filtering
- auto-scroll
- clear control
- camera and test context when available

The first implementation uses polling through:

```text
GET /api/runs/{id}/logs?after=<offset>
```

Future versions may add Server-Sent Events or WebSocket streaming.
