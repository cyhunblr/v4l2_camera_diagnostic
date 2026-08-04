# Continuous Integration

`v4l2-camera-diagnostic` utilizes two GitHub Actions workflows:

- [`.github/workflows/ci.yml`](../.github/workflows/ci.yml) runs full compilation, linting, and testing on every branch `push`.
- [`.github/workflows/pr.yml`](../.github/workflows/pr.yml) runs on `pull_request` events to validate the PR title (Conventional Commits format) and verify that the Push CI run for the PR HEAD commit completed successfully, preventing duplicate heavy CI runs.

Hardware-trigger diagnostics require a physical rig and are documented separately as an operator checklist in [`docs/backend/hardware-validation.md`](backend/hardware-validation.md).

## Jobs

### `quality-checks`

Gates every other job. Fails fast on:

- **Commit messages** — `wagoid/commitlint-github-action`, rules in
  [`commitlint.config.cjs`](../commitlint.config.cjs) (repo root, no
  `@commitlint/config-conventional` dependency — the type enum and
  header-length rule are defined inline).
- **Markdown** — `markdownlint '**/*.md'`, config in
  [`.markdownlint.json`](../.markdownlint.json) and
  [`.markdownlintignore`](../.markdownlintignore).
- **C++ style** — `cpplint` (config: [`CPPLINT.cfg`](../CPPLINT.cfg)) and
  `clang-format-18 --dry-run --Werror` (config:
  [`.clang-format`](../.clang-format)) against `source/backend/`.
- **Frontend lint** — `eslint .` in `source/frontend`
  ([`eslint.config.js`](../source/frontend/eslint.config.js)).

See [`docs/testing.md`](testing.md#static-analysis) for the exact local
commands and for why a few `cpplint` categories are deliberately disabled.

### `build-frontend` — "Test & Build Frontend"

In `source/frontend`, on **Node 22**:

1. `npm ci`
2. `npm test` — the Vitest suite (jsdom + React Testing Library)
3. `npm run build` — `tsc --noEmit && vite build`

Tests run before the build so a behaviour regression fails ahead of a bundle
that would still compile. Both this job and `quality-checks` pin Node 22, the
same major `installation.sh` provisions.

### `build-backend-amd64` / `build-backend-arm64`

Both build the CMake project (`v4l2-camera-diagnostic`,
`v4l2-camera-diagnostic-web`) and run the
CTest suite with `-DV4L2DIAG_ENABLE_WARNINGS_AS_ERRORS=ON`, across a
3-version Ubuntu matrix (`20.04`, `22.04`, `24.04`):

| Job | Runner | Ubuntu versions via |
| --- | --- | --- |
| `build-backend-amd64` | `ubuntu-latest` | `container: ubuntu:${{ matrix.ubuntu }}` |
| `build-backend-arm64` | `ubuntu-24.04-arm` (native) | `container: ubuntu:${{ matrix.ubuntu }}` |

Both jobs use the exact same steps and matrix — only `runs-on` differs. This
is deliberate: GitHub removed hosted `ubuntu-20.04` runner images in 2025, so
version coverage comes entirely from `container:`, not from `runs-on`. Docker
resolves `ubuntu:20.04` to the image manifest matching the *host*
architecture, so the ARM64 job runs 20.04/22.04/24.04 natively on real ARM64
hardware with **no QEMU or binfmt emulation** — it is not a slower
correctness check bolted on for completeness, it runs at native speed.

Because the container images are bare `ubuntu:X` with nothing preinstalled,
each job's first step installs `git` before `actions/checkout` can run, then
installs the real build dependencies:
`libgpiod-dev`, `libmicrohttpd-dev`, and `libjsoncpp-dev`.

Successful builds upload the three executables as workflow artifacts, named
`backend-<arch>-ubuntu-<version>`.

### Availability note

`ubuntu-24.04-arm` hosted runners are free for public repositories. Private
repositories need a GitHub plan that includes Linux ARM64 hosted runners —
confirm this before relying on `build-backend-arm64` in a private repo.

## Local reproduction

Most checks above have local equivalents in [`docs/testing.md`](testing.md),
and the backend/frontend build commands can be run before pushing. The hosted
ARM64 matrix and GitHub Actions services are still CI-specific.
