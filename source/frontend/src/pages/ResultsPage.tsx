import { useEffect, useState } from "react";
import { FileDown } from "lucide-react";
import { getRun, getRunReports } from "../api";
import { ResultsTable } from "../components/ResultsTable";
import { ReportLink, TestSummary, summariesFromResult } from "../types";
import { HistoricalResult, cacheHistoricalResult, getCachedHistoricalResult } from "./historicalResultsCache";

type Props = {
  viewedRunId: string | null;
  liveSummaries: TestSummary[];
  liveReportLinks: ReportLink[];
};

export function ResultsPage({ viewedRunId, liveSummaries, liveReportLinks }: Props) {
  const [historical, setHistorical] = useState<HistoricalResult | null>(null);
  const [loading, setLoading] = useState(false);

  useEffect(() => {
    if (!viewedRunId) {
      setHistorical(null);
      return;
    }
    const cached = getCachedHistoricalResult(viewedRunId);
    if (cached) {
      setHistorical(cached);
      return;
    }
    let cancelled = false;
    setLoading(true);
    // One structured source. The backend answers from memory while the run is live and
    // from the canonical JSON artifact after a restart -- indistinguishable from here,
    // and deliberately so: no disk fallback and no second parser on this side
    // (plan 2.6.1). Logs are no longer read for statuses; they are not persisted.
    //
    // allSettled, not all: the two requests are independent. With Promise.all a failing
    // /reports rejected the pair and threw away a perfectly good structured result --
    // and, the other way round, hid the backend's `reason` for an unavailable one.
    Promise.allSettled([getRun(viewedRunId), getRunReports(viewedRunId)])
      .then(async ([runOutcome, reportsOutcome]) => {
        // Artifact links, best effort. They come from their own endpoint, so a
        // downloadable HTML report stays usable even when the structured result does
        // not -- and a broken link fetch never costs us the results.
        let reportLinks: ReportLink[] = [];
        if (reportsOutcome.status === "fulfilled" && reportsOutcome.value.ok) {
          const reportsJson = await reportsOutcome.value.json().catch(() => ({}));
          reportLinks = reportsJson.reports ?? [];
        }

        if (runOutcome.status === "rejected") {
          if (!cancelled) {
            setHistorical({
              state: "unavailable",
              summaries: [],
              reportLinks,
              reason: "Could not reach the diagnostic server."
            });
          }
          return;
        }
        const runRes = runOutcome.value;
        if (!runRes.ok) {
          // The backend says what is wrong -- `structured_result_unavailable` plus a
          // reason. Surface it instead of rendering an empty table.
          const errorJson = await runRes.json().catch(() => ({}));
          const result: HistoricalResult = {
            state: "unavailable",
            summaries: [],
            reportLinks,
            reason: errorJson.reason || errorJson.error || `The server returned ${runRes.status}.`
          };
          if (!cancelled) setHistorical(result);  // deliberately NOT cached
          return;
        }
        const runJson = await runRes.json().catch(() => ({}));
        // A run that genuinely produced no tests IS an available result, so an empty
        // summaries list is cached like any other.
        const result: HistoricalResult = {
          state: "available",
          summaries: summariesFromResult(runJson.result),
          reportLinks,
          reason: ""
        };
        if (!cancelled) {
          cacheHistoricalResult(viewedRunId, result);
          setHistorical(result);
        }
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
  const unavailable = Boolean(viewedRunId) && historical?.state === "unavailable";

  return (
    <div className="page results-page">
      <header className="topbar">
        <div>
          <p className="eyebrow">Output</p>
          <h2>Result Output</h2>
          <span className="status-line">{viewedRunId ? `Run ${viewedRunId}` : "Current run"}</span>
        </div>
      </header>
      {reportLinks.length > 0 && (
        <div className="report-bar">
          <FileDown size={14} />
          <span>Reports:</span>
          {reportLinks.map((r) => (
            <a key={r.url} href={r.url} target="_blank" rel="noreferrer">{r.format.toUpperCase()}</a>
          ))}
        </div>
      )}
      <div className="panel results-panel full-height">
        {viewedRunId && loading ? (
          <div className="results-empty">Loading run results...</div>
        ) : unavailable ? (
          <div className="results-empty" role="status">
            <p>Structured results are unavailable for this run.</p>
            <p className="results-empty-reason">{historical?.reason}</p>
          </div>
        ) : (
          <ResultsTable summaries={summaries} />
        )}
      </div>
    </div>
  );
}
