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
- open JSON, Markdown, and HTML reports (the HTML report prints to PDF via the browser)

## Development

Requires **Node 22** — the project standard. CI and `installation.sh` use the
same major, and `engines` in `package.json` declares it. npm only warns on a
mismatch unless you opt into `engine-strict`, so treat Node 22 as the
supported version rather than something the tooling blocks.

```bash
npm ci                # npm install also works without a lockfile
npm run dev           # Vite dev server on 127.0.0.1
npm test              # Vitest suite (jsdom + React Testing Library)
npm run test:watch    # same suite, re-runs on change
npm run test:coverage # v8 coverage into coverage/ (git-ignored, lint-ignored)
npm run lint          # eslint . — must report zero warnings
npm run build         # tsc --noEmit && vite build
```

Tests sit next to the code they cover as `*.test.tsx`. Vitest is configured in
`vite.config.ts` (not a separate config) so the app and the tests share one
plugin setup; global setup is `src/test/setup.ts`.

In CI the `Test & Build Frontend` job runs `npm test` before `npm run build`,
so a behaviour regression fails ahead of a bundle that would still compile.
See `../../docs/testing.md` and `../../docs/ci.md`.

See `../../docs/frontend/web-ui.md` for runtime behavior and LAN access.
