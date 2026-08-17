# Reports

The terminal output is not the canonical diagnostic result. Every run should produce structured artifacts.

Every run generates all three artifacts:

- HTML
- JSON
- Markdown

The formats are **not selectable** -- not in the web UI, and not on the CLI
(there is no `--report` option). Every run writes all three, which is also what
lets the structured result be read back from the JSON artifact after a restart.

HTML reports include an **Export as PDF** control that uses the browser print
dialog, so they can be saved as PDF from the report page. No `pdf` artifact is
generated on either surface.

Example:

```bash
v4l2-camera-diagnostic run \
  --camera /dev/video0 \
  --tests stable \
  --output-dir reports
```

Report contents include:

- run metadata
- host name
- selected cameras
- selected device profile
- selected memory backends
- selected tests
- pass, fail, warn, skipped, and error statuses
- metric tables
- test details and warnings

HTML charts are derived from the metric keys a test emits. Two rules follow from that and are worth
knowing when reading a report:

- **A sweep is one chart per unit, not one chart per swept value.** A format or resolution sweep
  produces a single latency chart and a single throughput chart with the swept values as categories,
  because two measures on different scales never share one pair of axes.
- **A swept value that could not be measured emits no metrics, so it cannot be a chart category.**
  Those are listed under the chart as `Not measured (N of M enumerated)`, with the reason taken from
  the test's detail lines. This is what distinguishes "the camera only supports two formats" from
  "only two of its formats could be measured".

There is no `pdf` report format. The HTML report carries an **Export as PDF**
button that calls `window.print()` -- the same thing Ctrl+P does -- so the PDF
is produced by the browser, with its own paper size, margin and header/footer
options. The application generates no PDF artifact and depends on no PDF
converter (`wkhtmltopdf`, `weasyprint` and headless Chromium are all gone).
Print layout is controlled solely by the report's print CSS.

A stored run request from an older build keeps working: a `report_formats` field
is **ignored** outright. There is no fallback to `json,html` and no attempt to
honour the old selection -- the field selects nothing because every run already
writes all three artifacts.

JSON is intended for automation. Markdown and HTML are intended for human
review.
