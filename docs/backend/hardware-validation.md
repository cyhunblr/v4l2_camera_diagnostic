# Hardware Validation

Hardware diagnostics require a real camera, GPIO trigger wiring, and
permissions for `/dev/video*` and GPIO character devices. GitHub-hosted CI
cannot provide that hardware, so the main workflow only builds the software and
runs checks that do not need a physical rig.

Use this page as an operator checklist when validating diagnostics on target
hardware.

## Machine Setup

1. Use an isolated diagnostic machine or service account.
2. Add the user to groups that can access video and GPIO devices.
3. Install build/runtime dependencies:

   ```bash
   sudo apt-get update
   sudo apt-get install -y build-essential cmake pkg-config \
     libgpiod-dev libmicrohttpd-dev libjsoncpp-dev
   ```

4. Build the project:

   ```bash
   cmake -S . -B build -DV4L2DIAG_ENABLE_WARNINGS_AS_ERRORS=ON
   cmake --build build --parallel
   ```

## Suggested Run

Start with the `stable` tag and the memory backend you expect to use in
production:

```bash
./build/v4l2-camera-diagnostic run \
  --camera /dev/video0 \
  --trigger-mode hardware \
  --profile <local-profile-id> \
  --backend mmap \
  --tests stable \
```

The profile must bind every role the run expects (`master` here, plus `slave-N`
for each extra camera); the channel is resolved from the role rather than passed
on the command line. See
[`docs/development/cli.md`](../development/cli.md) for `--bind-role`.

Record the camera model, kernel, driver version, trigger profile id, the resolved
role routing, backend list, selected tests, and generated report artifacts with any
issue or release validation note.

## Safety

Hardware-trigger tests may change stream state, camera controls, and GPIO line
values. Run them only on an isolated diagnostic setup where no production
process is using the same camera or trigger line.
