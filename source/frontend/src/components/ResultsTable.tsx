import { TestSummary } from "../types";

type Props = {
  summaries: TestSummary[];
  emptyMessage?: string;
};

function displayStatus(status: string): string {
  switch (status.toLowerCase()) {
    case "pass": return "PASS";
    case "fail": return "FAIL";
    case "warn": return "WARN";
    case "skipped":
    case "skip": return "SKIP";
    case "done": return "PASS";
    default: return (status || "INFO").toUpperCase();
  }
}

function statusColor(status: string): string {
  switch (status.toLowerCase()) {
    case "pass":
    case "done": return "var(--success)";
    case "fail": return "var(--error)";
    case "warn": return "var(--warn)";
    case "skip":
    case "skipped": return "var(--warn)";
    default: return "var(--text-secondary)";
  }
}

function displaySummary(summary: TestSummary): string {
  return summary.message.replace(/^\u2713\s*/, "");
}

export function ResultsTable({
  summaries,
  emptyMessage = "No test results yet. Results appear as tests complete."
}: Props) {
  // The backend now emits exactly one summary line per test — the verdict, with the
  // duration folded in — so there is no longer a second "Completed in Nms" line to
  // de-duplicate by string-matching an English message.
  const rows = summaries;

  if (rows.length === 0) {
    return <div className="results-empty">{emptyMessage}</div>;
  }
  return (
    <table className="results-table">
      <thead>
        <tr>
          <th className="col-status">Status</th>
          <th className="col-test">Test</th>
          <th className="col-summary">Summary</th>
        </tr>
      </thead>
      <tbody>
        {rows.map((s, i) => (
          <tr key={i} className={`status-${s.status.toLowerCase()}`}>
            <td className="col-status">
              <span className="status-label" style={{ color: statusColor(s.status) }}>{displayStatus(s.status)}</span>
            </td>
            <td className="col-test">{s.test}</td>
            <td className="col-summary">{displaySummary(s)}</td>
          </tr>
        ))}
      </tbody>
    </table>
  );
}
