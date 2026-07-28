import { RefObject } from "react";
import { Square, Trash2 } from "lucide-react";
import { LogLine } from "../types";

function formatLogTimestamp(utcString: string | undefined): string {
  if (!utcString) return "";
  const match = utcString.match(/T(\d{2}:\d{2}:\d{2}(?:\.\d+)?)/);
  if (match) {
    return match[1].replace(/Z$/, "");
  }
  return utcString;
}

type Props = {
  logs: LogLine[];
  visibleLogs: LogLine[];
  severityFilter: string;
  onSeverityFilterChange: (value: string) => void;
  autoScroll: boolean;
  onAutoScrollChange: (value: boolean) => void;
  onClearLogs: () => void;
  isRunning: boolean;
  runStatus: string;
  elapsedSec: number;
  secSinceLastLog: number;
  actionInProgress: boolean;
  onRequestStop: () => void;
  outputRef: RefObject<HTMLDivElement>;
};

export function LiveOutputPage({
  logs,
  visibleLogs,
  severityFilter,
  onSeverityFilterChange,
  autoScroll,
  onAutoScrollChange,
  onClearLogs,
  isRunning,
  runStatus,
  elapsedSec,
  secSinceLastLog,
  actionInProgress,
  onRequestStop,
  outputRef
}: Props) {
  return (
    <div className="output-view">
      <header className="topbar">
        <div>
          <p className="eyebrow">Live Output</p>
          <h2>
            {runStatus === "idle" ? "No run started." : (
              <span className="status-line">
                Run status: <strong>{runStatus}</strong>
                {isRunning && (
                  <span className="elapsed">
                    {Math.floor(elapsedSec / 60)}:{String(elapsedSec % 60).padStart(2, "0")}
                  </span>
                )}
                {!isRunning && logs.length > 0 && <span className="elapsed">{logs.length} log lines</span>}
              </span>
            )}
          </h2>
        </div>
        <div className="output-header-actions">
          <label className="output-filter-control">
            <span>Level</span>
            <select value={severityFilter} onChange={(e) => onSeverityFilterChange(e.target.value)}>
              <option value="all">All</option>
              <option value="info">Info</option>
              <option value="warn">Warn</option>
              <option value="error">Error</option>
            </select>
          </label>
          <label className="toggle-control">
            <input type="checkbox" checked={autoScroll} onChange={(e) => onAutoScrollChange(e.target.checked)} />
            <span>Auto-scroll</span>
          </label>
          <button className="icon-button" onClick={onClearLogs} title="Clear logs" aria-label="Clear logs">
            <Trash2 size={16} />
          </button>
          {isRunning && (
            <button className="stop-pill" onClick={onRequestStop} disabled={actionInProgress}>
              <Square size={14} /> Stop
            </button>
          )}
        </div>
      </header>
      <div className="terminal-container full-height">
        <div className="terminal-header">
          <span className="col-time">TIME</span>
          <span className="col-level">LEVEL</span>
          <span className="col-source">SOURCE</span>
          <span className="col-msg">MESSAGE</span>
        </div>
        <div className="terminal-output" ref={outputRef}>
          {visibleLogs.length === 0 && (
            <div className="log-line muted">
              {isRunning
                ? "Running diagnostic, awaiting first output..."
                : "No log output yet. Start a diagnostic run from the sidebar."}
            </div>
          )}
          {visibleLogs.map((line) => (
            <div className={`log-line ${line.severity} ${line.log_type || "progress"}`} key={line.offset}>
              {line.log_type === "section_start" ? (
                <>
                  <span className="section-ts" title={line.timestamp_utc}>{formatLogTimestamp(line.timestamp_utc)}</span>
                  <code className="section-camera">{line.camera || "system"}</code>
                  <p className="section-title">{line.message}</p>
                </>
              ) : line.log_type === "data" ? (
                <>
                  <code className="data-camera">{line.camera || "system"}</code>
                  <pre className="data-block">{line.message}</pre>
                </>
              ) : (
                <>
                  <span title={line.timestamp_utc}>{formatLogTimestamp(line.timestamp_utc)}</span>
                  <strong>{line.severity}</strong>
                  <code>{line.camera || "system"}</code>
                  <p>{line.message}</p>
                </>
              )}
            </div>
          ))}
          {isRunning && visibleLogs.length > 0 && secSinceLastLog >= 2 && (
            <div className="log-line waiting">
              <span className="waiting-indicator">
                <span className="dot-bounce" />
                <span className="dot-bounce d2" />
                <span className="dot-bounce d3" />
              </span>
              <p>Running — {secSinceLastLog}s since last output</p>
            </div>
          )}
        </div>
      </div>
    </div>
  );
}
