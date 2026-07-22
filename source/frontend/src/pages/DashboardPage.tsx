import { useEffect, useState } from "react";
import { CheckCircle2, Clock, Gauge, History, XCircle } from "lucide-react";
import { getRuns } from "../api";
import { StatTile } from "../components/StatTile";
import { RunSummary } from "../types";

type Props = {
  onViewRun: (runId: string) => void;
};

function formatDuration(ms: number): string {
  const totalSeconds = Math.max(0, Math.round(ms / 1000));
  const minutes = Math.floor(totalSeconds / 60);
  const seconds = totalSeconds % 60;
  return `${minutes}:${String(seconds).padStart(2, "0")}`;
}

export function DashboardPage({ onViewRun }: Props) {
  const [runs, setRuns] = useState<RunSummary[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    let cancelled = false;
    getRuns()
      .then((data) => {
        if (!cancelled) {
          setRuns(data);
          setError(null);
        }
      })
      .catch(() => {
        // The GET /api/runs endpoint may not exist yet, or the server may be
        // unreachable — degrade to an empty-history state rather than crash.
        if (!cancelled) setError("Could not load run history. The server may not support run history yet.");
      })
      .finally(() => {
        if (!cancelled) setLoading(false);
      });
    return () => {
      cancelled = true;
    };
  }, []);

  const totals = runs.reduce(
    (acc, run) => {
      acc.pass += run.pass_count;
      acc.fail += run.fail_count;
      acc.warn += run.warn_count;
      acc.skip += run.skip_count;
      acc.duration += run.duration_ms;
      if (run.fail_count > 0) acc.runsWithFail += 1;
      return acc;
    },
    { pass: 0, fail: 0, warn: 0, skip: 0, duration: 0, runsWithFail: 0 }
  );
  const totalRuns = runs.length;
  const totalChecks = totals.pass + totals.fail + totals.warn + totals.skip;
  const passRate = totalChecks > 0 ? (totals.pass / totalChecks) * 100 : 0;
  const failRate = totalRuns > 0 ? (totals.runsWithFail / totalRuns) * 100 : 0;
  const avgDurationMs = totalRuns > 0 ? totals.duration / totalRuns : 0;

  return (
    <div className="page">
      <header className="topbar">
        <div>
          <p className="eyebrow">Diagnostic Control Center</p>
          <h2>Dashboard</h2>
        </div>
      </header>

      <div className="stat-tile-row">
        <StatTile icon={<Gauge size={20} />} label="Total Runs" value={totalRuns} />
        <StatTile icon={<CheckCircle2 size={20} />} label="Pass Rate" value={`${passRate.toFixed(1)}%`} tone="success" />
        <StatTile icon={<XCircle size={20} />} label="Runs With Failures" value={`${failRate.toFixed(1)}%`} tone="error" />
        <StatTile icon={<Clock size={20} />} label="Avg Duration" value={formatDuration(avgDurationMs)} />
      </div>

      <div className="panel">
        <div className="panel-title">
          <History size={18} />
          <h3>Recent Runs</h3>
        </div>
        {loading && <div className="empty">Loading run history...</div>}
        {!loading && error && <div className="empty">{error}</div>}
        {!loading && !error && runs.length === 0 && (
          <div className="empty">No runs recorded yet. Start a diagnostic to see history here.</div>
        )}
        {!loading && !error && runs.length > 0 && (
          <div className="table-scroll">
            <table className="results-table runs-table">
              <thead>
                <tr>
                  <th className="col-status">Status</th>
                  <th>Started</th>
                  <th>Cameras</th>
                  <th>Profile</th>
                  <th>Duration</th>
                  <th>Pass/Fail/Warn/Skip</th>
                  <th>Reports</th>
                </tr>
              </thead>
              <tbody>
                {runs.map((run) => (
                  <tr
                    key={run.id}
                    className={`status-${run.status.toLowerCase()} clickable-row`}
                    onClick={() => onViewRun(run.id)}
                  >
                    <td className="col-status">
                      <span className="status-badge">
                        {run.status === "completed" ? "✓" : run.status === "error" ? "✗" : run.status === "stopped" ? "⏹" : "…"}
                      </span>
                    </td>
                    <td>{run.started_at_utc}</td>
                    <td>{run.camera_paths.join(", ") || "—"}</td>
                    <td>{run.profile_id}</td>
                    <td>{formatDuration(run.duration_ms)}</td>
                    <td>{run.pass_count}/{run.fail_count}/{run.warn_count}/{run.skip_count}</td>
                    <td onClick={(e) => e.stopPropagation()}>
                      {run.reports.map((r) => (
                        <a key={r.url} href={r.url} target="_blank" rel="noreferrer" className="report-link-pill">
                          {r.format.toUpperCase()}
                        </a>
                      ))}
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        )}
      </div>
    </div>
  );
}
