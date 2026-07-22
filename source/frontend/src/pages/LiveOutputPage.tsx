import { RefObject } from "react";
import { Square } from "lucide-react";
import { LogLine } from "../types";

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
                {isRunning && <span className="pulse-dot" />}
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
          <select value={severityFilter} onChange={(e) => onSeverityFilterChange(e.target.value)}>
            <option value="all">all</option>
            <option value="info">info</option>
            <option value="warn">warn</option>
            <option value="error">error</option>
          </select>
          <label className="inline-checkbox">
            <input type="checkbox" checked={autoScroll} onChange={(e) => onAutoScrollChange(e.target.checked)} />
            auto-scroll
          </label>
          <button className="icon-button" onClick={onClearLogs} title="Clear logs">clear</button>
          {isRunning && (
            <button className="stop-pill" onClick={onRequestStop} disabled={actionInProgress}>
              <Square size={14} /> Stop
            </button>
          )}
        </div>
      </header>
      <div className="terminal-output full-height" ref={outputRef}>
        {visibleLogs.length === 0 && (
          <div className="log-line muted">
            {isRunning
              ? <><span className="pulse-dot" /> Running diagnostic, awaiting first output...</>
              : "No log output yet. Start a diagnostic run from the sidebar."}
          </div>
        )}
        {visibleLogs.map((line) => (
          <div className={`log-line ${line.severity} ${line.log_type || "progress"}`} key={line.offset}>
            {line.log_type === "section_start" ? (
              <>
                <span className="section-ts">{line.timestamp_utc}</span>
                <p className="section-title">{line.message}</p>
              </>
            ) : line.log_type === "data" ? (
              <pre className="data-block">{line.message}</pre>
            ) : (
              <>
                <span>{line.timestamp_utc}</span>
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
  );
}
