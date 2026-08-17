# Roadmap

This roadmap separates completed foundations from remaining work.
Implementation details belong in issues or focused design notes.

## Completed Foundations

- MPL-2.0 licensing, repository documentation, and contributor tooling
- CMake backend with CLI, web server, core library, and structured reports
- Device and V4L2 control discovery
- Local versioned profiles with web API/UI create, update, and removal
- Hardware GPIO, V4L2 software-trigger, and free-run trigger abstraction
- Camera-specific profile/channel assignments and visual trigger routing
- Camera/profile metadata for device matching and run defaults
- Memory backend, test, report, multi-camera, live output, and run history flows
- AMD64/ARM64 Ubuntu CI build matrix and code-quality checks

## Current Gaps

- The frontend has linting and type-checking, but no component or unit test
  suite yet.
- Physical trigger and camera behavior still requires manual validation on a
  real rig; hosted CI covers builds and hardware-independent tests.

## Next Work

- Add frontend unit/component tests around the main run and routing workflows.
- Continue physical validation across representative camera drivers and
  control-device topologies.
