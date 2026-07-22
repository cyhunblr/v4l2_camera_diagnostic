# Configuration

The web UI is the primary configuration surface for cameras, trigger routing,
profiles, memory backends, tests, and reports.

## Trigger Modes

A run uses exactly one mode:

- `hardware`: GPIO channels defined by local profiles
- `software`: V4L2 control recipes defined by local profiles
- `free-run`: no trigger profile or channel is required

The Trigger Routing view supports one profile for all cameras or per-camera
profiles. Drag a camera connection to a compatible channel. A camera has one
active channel; a channel can serve multiple cameras.

## Profiles

Profiles are local machine configuration and are not shipped by the project.
Create, update, or remove them in the web UI. Profiles use versioned JSON.

Profiles are stored under:

```text
$XDG_CONFIG_HOME/v4l2-camera-diagnostic/profiles
```

or, when `XDG_CONFIG_HOME` is unset:

```text
$HOME/.config/v4l2-camera-diagnostic/profiles
```

Profile metadata can match V4L2 driver, card, and bus information and provide
default trigger mode, memory backends, tests, reports, and camera-to-channel
bindings. Routing changes affect only the current run until explicitly saved
as profile defaults.

## Software Trigger Controls

The profile editor discovers writable controls on `/dev/video*` and
`/dev/v4l-subdev*`. A software channel records setup, fire, and teardown
control writes. The backend refuses a write when the live control ID, name,
type, or range no longer matches the profile.

## Memory Backends

Supported backend names are `mmap`, `dmabuf`, and `userptr`. The
`v4l2-memory-probe` test verifies their availability on the selected device.
