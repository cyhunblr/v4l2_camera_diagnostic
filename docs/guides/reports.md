# Reports

The terminal output is not the canonical diagnostic result. Every run should produce structured artifacts.

The web UI can generate:

- JSON
- Markdown
- HTML

HTML reports include an **Export as PDF** control that uses the browser print
dialog, so they can be saved as PDF from the report page. The web UI does not
currently expose PDF as a separate report format.

The CLI also supports PDF artifacts:

Example:

```bash
v4l2-camera-diagnostic run \
  --camera /dev/video0 \
  --tests implemented \
  --report json,md,html,pdf \
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

Requested PDF artifacts are generated from the same HTML report using
`wkhtmltopdf` first and `weasyprint` second. When neither converter is
installed, the report writer falls back to the same printable HTML content at
the requested `.pdf` path.

JSON is intended for automation. Markdown and HTML are intended for human
review.
