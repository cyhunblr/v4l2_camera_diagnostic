import { useEffect, useState } from "react";
import { CheckCircle2, Clock, Gauge, History, Play, Route, XCircle } from "lucide-react";
import { getRuns } from "../api";
import { StatTile } from "../components/StatTile";
import { RunSummary } from "../types";
import { overallAverageDurationMs, overallPassRate } from "../passRate";
import { formatDuration } from "../formatDuration";

type Props = {
  onViewRun: (runId: string) => void;
  onStartNewDiagnostic: () => void;
  isRunning: boolean;
};

// The backend already emits UTC as "%Y-%m-%dT%H:%M:%SZ", so this is a pure string
// transform to the same readable form the HTML report uses ("... UTC"). Deliberately
// not via Date(), which would re-render the instant in the viewer's local timezone.
function formatUtc(timestamp: string): string {
  const match = timestamp.match(/^(\d{4}-\d{2}-\d{2})T(\d{2}:\d{2}:\d{2})Z$/);
  return match ? `${match[1]} ${match[2]} UTC` : timestamp;
}

export function DashboardPage({ onViewRun, onStartNewDiagnostic, isRunning }: Props) {
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

  const totalRuns = runs.length;
  // The rates come from the shared helper (plan 2.8): one run, one vote, skips excluded,
  // and only completed, untruncated runs with a real verdict counted. This page used to
  // pool every run's counters and divide once, which let a multi-camera run dominate.
  // Its only job now is formatting.
  const passRate = overallPassRate(runs);
  const avgDurationMs = overallAverageDurationMs(runs);
  const runsWithFail = runs.filter((run) => run.fail_count > 0).length;
  const failRate = totalRuns > 0 ? (runsWithFail / totalRuns) * 100 : 0;

  // null means "nothing to measure", which is not 0% -- 0% would read as "everything
  // failed".
  const passRateLabel = passRate === null ? "N/A" : `${(passRate * 100).toFixed(1)}%`;
  const avgDurationLabel = avgDurationMs === null ? "N/A" : formatDuration(avgDurationMs);

  return (
    <div className="page">
      <header className="topbar">
        <div>
          <p className="eyebrow">Diagnostic Control Center</p>
          <h2>Dashboard</h2>
        </div>
      </header>

      <section className="dashboard-command-panel">
        <div>
          <div className="panel-title">
            <Route size={18} />
            <h3>Diagnostic Flow</h3>
          </div>
          <p className="panel-hint">
            Start a guided run, then move through Cameras, Profiles, Test Selection, and Test Configuration in order.
          </p>
        </div>
        <div className="dashboard-flow-action">
          <button
            className="icon-text-button primary-action"
            onClick={onStartNewDiagnostic}
            disabled={isRunning}
            title={isRunning ? "Stop the current diagnostic before starting a new one." : undefined}
          >
            <Play size={16} /> Start a New Diagnostic
          </button>
        </div>
      </section>

      <div className="stat-tile-row">
        <StatTile icon={<Gauge size={20} />} label="Total Runs" value={totalRuns} />
        <StatTile icon={<CheckCircle2 size={20} />} label="Pass Rate" value={passRateLabel} tone="success" />
        <StatTile icon={<XCircle size={20} />} label="Runs With Failures" value={`${failRate.toFixed(1)}%`} tone="error" />
        <StatTile icon={<Clock size={20} />} label="Avg Duration" value={avgDurationLabel} />
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
                  <th>Started</th>
                  <th>Cameras</th>
                  <th>Profile</th>
                  <th>Duration</th>
                  <th>Pass/Fail/Warn/Skip</th>
                  <th className="col-status">Status</th>
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
                    <td>{formatUtc(run.started_at_utc)}</td>
                    <td>{run.camera_paths.join(", ") || "—"}</td>
                    {/*
                      Free-run genuinely has no Trigger Profile, which is different from
                      "a triggered run whose profile we failed to record". The mode says
                      which case this is, so the cell can be explicit instead of showing
                      a bare dash for both (plan 2.7).
                    */}
                    <td>
                      {run.trigger_mode === "free-run"
                        ? "Not required (free-run)"
                        : run.trigger_profile_id || "—"}
                    </td>
                    <td>{formatDuration(run.duration_ms)}</td>
                    <td>{run.pass_count}/{run.fail_count}/{run.warn_count}/{run.skip_count}</td>
                    <td className="col-status">
                      <span className="status-badge">
                        {run.status === "completed" ? "✓" : run.status === "error" ? "✗" : run.status === "stopped" ? "⏹" : "…"}
                      </span>
                    </td>
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
