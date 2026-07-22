# CLI (Developer Tool)

`v4l2-camera-diagnostic` is intended for scripting, debugging, and integration.
The web application is the primary user interface.

## Commands

```text
v4l2-camera-diagnostic list-devices
v4l2-camera-diagnostic tests list [--all]
v4l2-camera-diagnostic profiles list [--config-dir DIR]
v4l2-camera-diagnostic profiles add --id ID --name NAME --gpio INDEX:CHIP:LINE:LABEL [--config-dir DIR]
v4l2-camera-diagnostic profiles remove --id ID [--config-dir DIR]
v4l2-camera-diagnostic run [options]
```

The simple `profiles add --gpio` command creates a hardware profile. Use the
web profile editor for V4L2 software-control recipes, camera metadata, and
visual default routing.

## Run Options

| Option | Default | Notes |
| --- | --- | --- |
| `--camera PATH` | interactive discovery | repeatable or comma-separated |
| `--trigger-mode MODE` | `free-run` | `hardware`, `software`, or `free-run` |
| `--profile ID` | none | required by active trigger modes |
| `--trigger-channel ID` | auto when unique | one compatible channel must resolve |
| `--backend LIST` | `mmap` | `mmap`, `dmabuf`, `userptr` |
| `--tests LIST` | `implemented` | test ids, categories, or group selectors |
| `--report LIST` | `json,html` | `json`, `markdown`, `html`, `pdf` |
| `--output-dir DIR` | `reports` | report directory |
| `--run-mode MODE` | `sequential` | `sequential` or `parallel` |
| `--include-long` | off | include long-running tests |
| `--include-experimental` | off | include experimental tests |

Example:

```bash
v4l2-camera-diagnostic run \
  --camera /dev/video0 \
  --trigger-mode hardware \
  --profile <local-profile-id> \
  --trigger-channel <local-channel-id> \
  --backend mmap,dmabuf \
  --tests implemented \
  --report json,html
```

Profiles are stored in the local configuration directory documented in
[`docs/guides/configuration.md`](../guides/configuration.md).
