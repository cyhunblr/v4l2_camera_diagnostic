# Web UI

This directory contains the Vite/React frontend served by
`v4l2-camera-diagnostic-web`.

The UI is the primary human-facing surface for:

- discover cameras
- select hardware, software, or free-run trigger mode
- create, update, and remove local profiles
- route cameras to compatible profile channels
- select memory backends
- select tests
- start a diagnostic run
- monitor live progress and logs
- open JSON, Markdown, HTML, and PDF reports

Development commands:

```bash
npm install
npm run lint
npm run build
```

See `../../docs/frontend/web-ui.md` for runtime behavior and LAN access.
