# Uninstallation

```bash
./uninstallation.sh [--purge] [--clean] [--yes] [--dry-run]
```

By default the script asks two interactive yes/no questions (skipped, and
treated as "no", when run non-interactively without `--yes`):

- *"Remove user config, cache, and state?"* — corresponds to `--purge`.
- *"Remove local build artifacts while preserving reports?"* — corresponds
  to `--clean`.

## Always removed

Regardless of flags, these are deleted:

| Path | What it is |
| --- | --- |
| `~/.local/bin/v4l2-camera-diagnostic` | CLI binary |
| `~/.local/bin/v4l2-camera-diagnostic-web` | web app binary |
| `~/.local/share/applications/v4l2-camera-diagnostic.desktop` | desktop entry |
| `~/.local/share/v4l2-camera-diagnostic/web` | installed web UI assets |
| `~/.local/share/v4l2-camera-diagnostic/docs` | installed docs copy |

The now-empty `~/.local/share/v4l2-camera-diagnostic` directory itself is
removed (`rmdir`, silently skipped if anything else still lives in it).

## `--purge`

Additionally removes:

- `~/.config/v4l2-camera-diagnostic` — user-added device profiles
- `~/.cache/v4l2-camera-diagnostic`
- `~/.local/state/v4l2-camera-diagnostic`

Without `--purge`, user profiles and any cached/state data survive the
uninstall — a later reinstall picks them back up unchanged.

## `--clean`

Removes local, reproducible development artifacts from the repository
checkout:

- `build/` (the CMake build directory)
- `source/frontend/dist`, `source/frontend/node_modules`,
  `source/frontend/package-lock.json`
- the npm cache (`npm cache clean --force`, only if `npm` is reachable)

**Generated reports are never deleted, by any flag combination** — they
live under `$XDG_DATA_HOME/v4l2-camera-diagnostic/reports` (or
`~/.local/share/v4l2-camera-diagnostic/reports`), a path this script never
touches. See [`docs/frontend/web-ui.md`](../frontend/web-ui.md#report-storage).

## Common invocations

Non-interactive, remove installed files, user config, cache, and local build
artifacts while preserving generated reports:

```bash
./uninstallation.sh --yes --purge --clean
```

Preview what a full removal would do without deleting anything:

```bash
./uninstallation.sh --dry-run --yes --purge --clean
```

Keep user profiles/config but reclaim disk space from `build/` and
`node_modules`:

```bash
./uninstallation.sh --yes --clean
```
