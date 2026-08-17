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

Generated reports and the Dashboard's run history (`runs-index.json`) are written under a stable, absolute directory — not the process's current working directory — so they persist across launches regardless of how the app is started (terminal, service wrapper, different working directory):

```text
$XDG_DATA_HOME/v4l2-camera-diagnostic/reports
```

or, if `XDG_DATA_HOME` is unset:

```text
$HOME/.local/share/v4l2-camera-diagnostic/reports
```

Use `--report-root DIR` to override this location. `uninstallation.sh` never deletes this directory (including with `--purge`), so reports and run history survive an uninstall/reinstall cycle.

## Main Features

- discovered camera list, with single-camera or master/slave multi-camera selection
- hardware, software, and free-run trigger modes
- visual camera-to-channel routing
- local profile creation, update, and removal
- test selection (includes memory backend selection)
- run progress
- Live Output panel — the main screen to watch during a run
- Results panel with report download links

## Camera Selection

Choose **Single camera** to run the full diagnostic suite against one camera,
or **Multi-camera** to also pick slave cameras that only participate in the
t25 multi-camera cross-jitter test. The selected master is excluded from the
slave list automatically.

## Profile Management

Profiles are local machine configuration. The Profiles view can create and
remove profiles, and discover controls on video and subdevice nodes.

Every run selects exactly one trigger mode. In hardware or software mode,
camera nodes on the left connect to compatible profile channels on the right.
Use **Single profile** to apply one rig profile to every selected camera, or
**Per camera** to route cameras across different profiles. Free-run mode does
not require routing.

## Live Output

The Live Output panel is the main screen to watch while a run is in progress:
it shows runtime log lines from the diagnostic run and supports:

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

## Results

The Results view is disabled in the sidebar while a run is in progress — it
becomes available once the run finishes. It shows the run's report download
links (JSON/Markdown/HTML/PDF) at the top, followed by a per-test results
table that scrolls internally so the page itself doesn't grow with the
number of tests.
