import { useEffect, useState } from "react";
import { FileDown, Terminal } from "lucide-react";
import { getRunLogs, getRunReports } from "../api";
import { ResultsTable } from "../components/ResultsTable";
import { LogLine, ReportLink, TestSummary } from "../types";

type HistoricalResult = { summaries: TestSummary[]; reportLinks: ReportLink[] };

/** Cache historical run results so revisiting the Results page for the same
 *  run doesn't refetch (per the plan's "small Map" cache). */
const historicalCache = new Map<string, HistoricalResult>();

function deriveSummaries(logs: LogLine[]): TestSummary[] {
  const summaries: TestSummary[] = [];
  for (const line of logs) {
    if (line.log_type === "summary") {
      const match = line.message.match(/^(.+?) \[(.+?)\] (.+)$/);
      if (match) {
        summaries.push({ test: match[1], status: match[2], message: match[3], camera: line.camera });
      } else {
        summaries.push({ test: line.test, status: "done", message: line.message, camera: line.camera });
      }
    }
  }
  return summaries;
}

type Props = {
  viewedRunId: string | null;
  liveSummaries: TestSummary[];
  liveReportLinks: ReportLink[];
  liveRunStatus: string;
};

export function ResultsPage({ viewedRunId, liveSummaries, liveReportLinks, liveRunStatus }: Props) {
  const [historical, setHistorical] = useState<HistoricalResult | null>(null);
  const [loading, setLoading] = useState(false);

  useEffect(() => {
    if (!viewedRunId) {
      setHistorical(null);
      return;
    }
    const cached = historicalCache.get(viewedRunId);
    if (cached) {
      setHistorical(cached);
      return;
    }
    let cancelled = false;
    setLoading(true);
    Promise.all([getRunLogs(viewedRunId, 0), getRunReports(viewedRunId)])
      .then(async ([logsRes, reportsRes]) => {
        const logsJson = logsRes.ok ? await logsRes.json() : { lines: [] };
        const reportsJson = reportsRes.ok ? await reportsRes.json() : { reports: [] };
        const result: HistoricalResult = {
          summaries: deriveSummaries(Array.isArray(logsJson.lines) ? logsJson.lines : []),
          reportLinks: reportsJson.reports ?? []
        };
        if (!cancelled) {
          historicalCache.set(viewedRunId, result);
          setHistorical(result);
        }
      })
      .catch(() => {
        if (!cancelled) setHistorical({ summaries: [], reportLinks: [] });
      })
      .finally(() => {
        if (!cancelled) setLoading(false);
      });
    return () => {
      cancelled = true;
    };
  }, [viewedRunId]);

  const summaries = viewedRunId ? historical?.summaries ?? [] : liveSummaries;
  const reportLinks = viewedRunId ? historical?.reportLinks ?? [] : liveReportLinks;

  return (
    <div className="page">
      <header className="topbar">
        <div>
          <p className="eyebrow">{viewedRunId ? `Run ${viewedRunId}` : "Current Run"}</p>
          <h2>Results{!viewedRunId && ` — ${liveRunStatus}`}</h2>
        </div>
      </header>
      <div className="panel results-panel full-height">
        {viewedRunId && loading ? (
          <div className="results-empty">Loading run results...</div>
        ) : (
          <ResultsTable summaries={summaries} />
        )}
      </div>
      {reportLinks.length > 0 && (
        <div className="report-bar">
          <FileDown size={14} />
          <span>Reports:</span>
          {reportLinks.map((r) => (
            <a key={r.url} href={r.url} target="_blank" rel="noreferrer">{r.format.toUpperCase()}</a>
          ))}
          <button
            className="icon-button dmesg-export-btn"
            title="Export full boot dmesg"
            onClick={() => {
              fetch("/api/dmesg")
                .then(async (res) => {
                  if (!res.ok) {
                    throw new Error(await res.text() || `dmesg export failed (${res.status})`);
                  }
                  return res.text();
                })
                .then((text) => {
                  const blob = new Blob([text], { type: "text/plain" });
                  const url = URL.createObjectURL(blob);
                  const a = document.createElement("a");
                  a.href = url;
                  a.download = "dmesg.txt";
                  a.click();
                  URL.revokeObjectURL(url);
                })
                .catch((err) => {
                  window.alert(err instanceof Error ? err.message : "Failed to export dmesg.");
                });
            }}
          >
            <Terminal size={14} />
            <span>Export DMESG</span>
          </button>
        </div>
      )}
    </div>
  );
}
