# Contributing

Thank you for helping improve `v4l2-camera-diagnostic`.

## Project Language

All repository content should be written in English:

- source code
- comments
- CLI help text
- documentation
- report labels
- issue and pull request templates

## Development Loop

```bash
cmake -S . -B build -DV4L2DIAG_ENABLE_WARNINGS_AS_ERRORS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

## Local Git Hooks

The recommended contributor setup is:

```bash
scripts/setup-dev-env.sh
```

It installs the local CI-style developer tools where possible, installs
frontend dependencies, and enables the repository Git hooks. If the tools are
already installed and you only want to activate hooks for this clone, run:

```bash
scripts/install-git-hooks.sh
```

Git does not enable versioned hooks automatically after clone. The hook-only
installer sets the local `core.hooksPath` to `.githooks`. After hooks are
enabled:

- `pre-commit` runs the CI-matching C++ formatting check with `clang-format-18`
- `commit-msg` checks the Conventional Commit type, non-empty subject,
  lower-case type, and 100-character header limit
- `pre-push` runs the C++ formatting check again before pushing

If `clang-format-18` is missing, the pre-commit hook fails with install
instructions instead of letting a formatting mismatch reach GitHub Actions.

Before opening a pull request, run the build and tests locally, and run the
same static analysis CI runs (`cpplint`, `clang-format-18 --dry-run --Werror`,
and - for frontend changes - `npm run lint` / `npm run build` in
`source/frontend`). See [`docs/testing.md`](docs/testing.md) for every local
command and [`docs/ci.md`](docs/ci.md) for what runs in GitHub Actions.

Hardware-trigger tests should be documented with the camera, profile, kernel,
driver, and report artifacts used for validation.

## Running the CLI

`v4l2-camera-diagnostic` is a developer/integrator tool rather than the
end-user product. See [`docs/development/cli.md`](docs/development/cli.md).
End users interact with `v4l2-camera-diagnostic-web`.

## Commit Messages

Commit messages are checked locally by the optional `commit-msg` hook and
linted again in CI (see [`commitlint.config.cjs`](commitlint.config.cjs)).
They must start with one of:
`feat`, `fix`, `docs`, `style`, `refactor`, `perf`, `test`, `build`, `ci`,
`chore`, `revert` — e.g. `fix: correct GPIO trigger fallback path`.

## Test Migration Rule

When migrating a legacy hardware test into the structured core runner:

- keep the legacy behavior observable
- return structured `TestResult` data
- avoid direct `printf` as the source of truth
- use monotonic time for latency measurements
- place hard-coded timings, frame sizes, formats, and controls behind config defaults
- mark hardware-specific tests as profile-scoped when appropriate
