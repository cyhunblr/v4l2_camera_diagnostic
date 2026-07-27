import { TestSummary } from "../types";

type Props = {
  summaries: TestSummary[];
  emptyMessage?: string;
};

function isCompletionSummary(message: string): boolean {
  return /^(?:\u2713\s*)?Completed in \d+ms$/i.test(message.trim());
}

function displayStatus(status: string): string {
  switch (status.toLowerCase()) {
    case "pass": return "Pass";
    case "fail": return "Fail";
    case "warn": return "Warn";
    case "skipped": return "Skipped";
    case "done": return "Info";
    default: return status || "Info";
  }
}

function statusColor(status: string): string {
  switch (status.toLowerCase()) {
    case "pass": return "var(--success)";
    case "done": return "var(--success)";
    case "fail": return "var(--error)";
    case "warn": return "var(--warn)";
    case "skipped": return "var(--warn)";
    default: return "var(--text-secondary)";
  }
}

function displaySummary(summary: TestSummary): string {
  if (summary.status.toLowerCase() === "pass" || isCompletionSummary(summary.message)) {
    return "Completed";
  }
  return summary.message.replace(/^\u2713\s*/, "");
}

export function ResultsTable({
  summaries,
  emptyMessage = "No test results yet. Results appear as tests complete."
}: Props) {
  const completionKeys = new Set(
    summaries
      .filter((summary) => isCompletionSummary(summary.message))
      .map((summary) => `${summary.camera}:${summary.test}`)
  );
  const finalKeys = new Set(
    summaries
      .filter((summary) => !isCompletionSummary(summary.message))
      .map((summary) => `${summary.camera}:${summary.test}`)
  );
  const rows = summaries.filter((summary) => {
    const key = `${summary.camera}:${summary.test}`;
    return !completionKeys.has(key) || !finalKeys.has(key) || !isCompletionSummary(summary.message);
  });

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
