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

Start with stable implemented tests and the memory backend you expect to use in
production:

```bash
./build/v4l2-camera-diagnostic run \
  --camera /dev/video0 \
  --trigger-mode hardware \
  --profile <local-profile-id> \
  --trigger-channel <local-channel-id> \
  --backend mmap \
  --tests stable \
  --report json,md,html
```

Record the camera model, kernel, driver version, profile id, trigger channel,
backend list, selected tests, and generated report artifacts with any issue or
release validation note.

## Safety

Hardware-trigger tests may change stream state, camera controls, and GPIO line
values. Run them only on an isolated diagnostic setup where no production
process is using the same camera or trigger line.
