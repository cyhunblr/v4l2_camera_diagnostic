# CLI (Developer Tool)

`v4l2-camera-diagnostic` is intended for scripting, debugging, and integration.
The web application is the primary user interface.

## Commands

```text
v4l2-camera-diagnostic list-devices
v4l2-camera-diagnostic tests list [--all]
v4l2-camera-diagnostic profiles list [--config-dir DIR]
v4l2-camera-diagnostic profiles add --id ID --name NAME --gpio INDEX:CHIP:LINE:LABEL \
                                    --bind-role ROLE:CHANNEL_ID [--config-dir DIR]
v4l2-camera-diagnostic profiles remove --id ID [--config-dir DIR]
v4l2-camera-diagnostic run [options]
```

The simple `profiles add --gpio` command creates a hardware profile. Use the
web profile editor for V4L2 software-control recipes and visual routing.

### Profile Options (`profiles add`)

| Option | Notes |
| --- | --- |
| `--gpio INDEX:CHIP:LINE:LABEL` | hardware trigger channel, given the id `gpio-<INDEX>`. Repeatable |
| `--bind-role ROLE:CHANNEL_ID` | binds a run role to a channel. Repeatable. **Required** |

Roles are not free text. A Trigger Profile does not identify a physical camera —
`{driver, card, bus_info}` cannot tell this hardware's four video nodes apart — so
routing goes through run roles: `master`, then `slave-1`, `slave-2`, … numbered by
position in the run's slave list. A camera moving to a different `/dev/videoN`
keeps its role.

No channel is ever bound to a role automatically, **not even when the profile has
exactly one channel**. Routing a trigger to the wrong camera is a worse outcome
than asking for one more argument.

```bash
v4l2-camera-diagnostic profiles add \
  --id rig --name "Test Rig" \
  --gpio 0:0:100:MASTER --gpio 1:0:101:SLAVE \
  --bind-role master:gpio-0 \
  --bind-role slave-1:gpio-1
```

## Run Options

| Option | Default | Notes |
| --- | --- | --- |
| `--camera PATH` | interactive discovery | repeatable or comma-separated |
| `--trigger-mode MODE` | `free-run` | `hardware`, `software`, or `free-run` |
| `--profile ID` | none | the single run-level Trigger Profile. Required by active trigger modes |
| `--backend LIST` | `mmap` | `mmap`, `dmabuf`, `userptr` |
| `--tests LIST` | `stable` | test ids, categories, tags, or `all`. Every selector resolves directly: an **exact id** selects that one test, a category or tag selects its members, and `all` selects every registered test. Nothing is gated or filtered afterwards, so a long-running test named by id (or reached through `all`) does run |
| `--output-dir DIR` | `reports` | report directory |
| `--run-mode MODE` | `sequential` | `sequential` or `parallel` |

Example:

```bash
v4l2-camera-diagnostic run \
  --camera /dev/video0 \
  --trigger-mode hardware \
  --profile <local-profile-id> \
  --backend mmap,dmabuf \
  --tests stable \
```

There is no `--report` either: every run writes all three artifacts — HTML, JSON and
Markdown. The user does not choose formats, so there is nothing to pass.

There is no `--trigger-channel`: the channel each camera fires on is resolved from
its role and the profile's `role_bindings`. If the profile does not bind every role
the run expects, the run is refused before it starts — with the same message the
web API returns, because both go through one shared resolver. For example, running
two cameras against a profile that only binds `master` reports the missing
`slave-1` binding rather than completing it.

Free-run neither needs nor uses a Trigger Profile.

Profiles are stored in the local configuration directory documented in
[`docs/guides/configuration.md`](../guides/configuration.md).
