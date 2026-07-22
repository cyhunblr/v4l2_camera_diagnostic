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

Requested PDF artifacts are generated from the same HTML report using
`wkhtmltopdf` first and `weasyprint` second. When neither converter is
installed, the report writer falls back to the same printable HTML content at
the requested `.pdf` path.

JSON is intended for automation. Markdown and HTML are intended for human
review.
