import { TestSummary } from "../types";

type Props = {
  summaries: TestSummary[];
  emptyMessage?: string;
};

function statusColor(status: string): string {
  switch (status.toLowerCase()) {
    case "pass": return "var(--success)";
    case "fail": return "var(--error)";
    case "warn": return "var(--warn)";
    default: return "var(--text-secondary)";
  }
}

export function ResultsTable({
  summaries,
  emptyMessage = "No test results yet. Results appear as tests complete."
}: Props) {
  if (summaries.length === 0) {
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
        {summaries.map((s, i) => (
          <tr key={i} className={`status-${s.status.toLowerCase()}`}>
            <td className="col-status">
              <span
                className="status-dot-indicator"
                style={{ backgroundColor: statusColor(s.status) }}
                title={s.status}
              />
            </td>
            <td className="col-test">{s.test}</td>
            <td className="col-summary">{s.message}</td>
          </tr>
        ))}
      </tbody>
    </table>
  );
}
