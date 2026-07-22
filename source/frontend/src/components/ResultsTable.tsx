import { TestSummary } from "../types";

type Props = {
  summaries: TestSummary[];
  emptyMessage?: string;
};

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
              <span className="status-badge">
                {s.status === "Pass" ? "✓" : s.status === "Fail" ? "✗" : s.status === "Warn" ? "⚠" : "⏭"}
              </span>
            </td>
            <td className="col-test">{s.test}</td>
            <td className="col-summary">{s.message}</td>
          </tr>
        ))}
      </tbody>
    </table>
  );
}
