# Architecture

`v4l2-camera-diagnostic` is split into explicit backend, hardware, web, CLI,
and frontend components.

- device discovery enumerates `/dev/video*` capture devices
- control discovery enumerates V4L2 controls on video and subdevice nodes
- profile registry loads versioned JSON profiles from the user configuration
  directory
- trigger sources provide hardware GPIO, V4L2 software, and free-run modes
- runner executes selected tests for camera-specific profile/channel assignments
- report writer emits JSON, Markdown, HTML, and CLI-requested PDF artifacts
- web server exposes device/control discovery, profile CRUD, trigger tests,
  runs, live logs, historical runs, and report downloads
- frontend provides the browser workflow for camera selection, trigger
  routing, tests, report formats, live output, and results

## Directory Layout

```text
source/
  backend/
    core/
      include/v4l2diag/core/   public core headers
      src/                     runner, profiles, reports, tests, shared types
    hw/
      include/v4l2diag/hw/     public hardware headers
      src/                     camera/control discovery, trigger sources,
                               V4L2 capture
    web/
      include/v4l2diag/web/    public web-server headers
      src/                     HTTP server and JSON API
    cli/
      include/v4l2diag/cli/    public CLI headers
      src/                     CLI and web-server entry points
  frontend/
    src/                       React/TypeScript web UI
    public/                    static frontend assets
tests/                         hardware-independent backend tests
```

The CMake target `v4l2diag_core` contains the core and hardware
implementation units. The CLI and web-server executables link that target;
the web executable also compiles the HTTP server implementation. Public
includes use the `v4l2diag/<component>/...` prefix.

## Trigger Model

Every run selects exactly one trigger mode: `hardware`, `software`, or
`free-run`.

Hardware and software profiles expose named trigger channels. Each selected
camera carries its own profile and channel assignment. Cameras that resolve to
the same physical trigger resource are executed sequentially; independent
resources may execute in parallel when the run mode is `parallel`. The default
run mode is sequential. Free-run does not require a profile or channel.

Software channels store a V4L2 control recipe with setup, fire, and teardown
writes. A control device can be the capture node or a video/subdevice selected
by stable driver, card, bus, and sysfs metadata. Runtime validation compares
control ID, name, type, and value range before writing.

## Profiles

Profiles are local machine configuration, loaded from
`$XDG_CONFIG_HOME/v4l2-camera-diagnostic/profiles` or
`~/.config/v4l2-camera-diagnostic/profiles`. Versioned JSON profiles are the
format used by both the registry and the web API.

Profile defaults can include trigger mode, memory backends, test selectors,
report formats, and camera-to-channel bindings. Camera matching uses V4L2
driver, card, and bus metadata.

## HTTP API

The embedded web server is based on `libmicrohttpd`. It serves static frontend
assets, generated reports, and JSON endpoints:

- `GET /api/health`
- `GET /api/devices`
- `GET /api/control-devices`
- `GET /api/profiles`
- `POST /api/profiles`
- `POST /api/profiles/validate`
- `PUT /api/profiles/{id}`
- `DELETE /api/profiles/{id}`
- `POST /api/triggers/test`
- `GET /api/tests`
- `POST /api/runs`
- `GET /api/runs`
- `GET /api/runs/{id}`
- `GET /api/runs/{id}/logs?after=<offset>`
- `GET /api/runs/{id}/reports`
- `POST /api/runs/{id}/stop`

Run execution happens asynchronously. The server stores per-run status, log
offsets, report artifacts, and a `runs-index.json` history under the report
root.

## Frontend

The frontend is a Vite/React application under `source/frontend`. It keeps the
current run configuration in client state and moves the operator through:

- dashboard and historical runs
- camera selection
- profile selection and trigger routing
- test selection
- report format selection
- live output
- results

The report format page exposes JSON, Markdown, and HTML. HTML reports include
an in-report browser print action for saving as PDF. CLI runs can still request
PDF artifacts directly.

## Timekeeping

Trigger and capture latency measurements use captured trigger timestamps.
Wall-clock timestamps are reserved for run metadata and report labels.
