import { useEffect, useRef, useState } from "react";
import * as api from "../api";
import { LogLine, ReportLink, StartRunPayload } from "../types";

const RUN_ID_KEY = "v4l2diag_run_id";
const RUN_STATUS_KEY = "v4l2diag_run_status";

/**
 * Owns the live-run lifecycle: 800ms log polling, localStorage run-id
 * persistence (so logs survive a page refresh), and the elapsed/waiting
 * timers. Extracted verbatim from the original single-file App().
 */
export function useRunPolling(onError: (message: string | null) => void) {
  const [runId, setRunId] = useState<string | null>(null);
  const [runStatus, setRunStatus] = useState("idle");
  const [logs, setLogs] = useState<LogLine[]>([]);
  const [nextOffset, setNextOffset] = useState(0);
  const [reportLinks, setReportLinks] = useState<ReportLink[]>([]);
  const [elapsedSec, setElapsedSec] = useState(0);
  const [secSinceLastLog, setSecSinceLastLog] = useState(0);
  const [actionInProgress, setActionInProgress] = useState(false);
  const runStartRef = useRef<number | null>(null);
  const lastLogTimeRef = useRef<number>(Date.now());

  const isRunning = runStatus === "queued" || runStatus === "running";

  // Elapsed time counter while running.
  useEffect(() => {
    if (isRunning) {
      if (!runStartRef.current) runStartRef.current = Date.now();
      const timer = window.setInterval(() => {
        setElapsedSec(Math.floor((Date.now() - runStartRef.current!) / 1000));
        setSecSinceLastLog(Math.floor((Date.now() - lastLogTimeRef.current) / 1000));
      }, 1000);
      return () => window.clearInterval(timer);
    } else {
      runStartRef.current = null;
      setSecSinceLastLog(0);
    }
  }, [isRunning]);

  // Track when the last log line arrived.
  useEffect(() => {
    if (logs.length > 0) {
      lastLogTimeRef.current = Date.now();
      setSecSinceLastLog(0);
    }
  }, [logs.length]);

  // Restore last run from localStorage so logs survive a page refresh.
  useEffect(() => {
    const savedId = localStorage.getItem(RUN_ID_KEY);
    const savedStatus = localStorage.getItem(RUN_STATUS_KEY) ?? "completed";
    if (savedId) {
      setRunId(savedId);
      setRunStatus(savedStatus);
      api
        .getRunLogs(savedId, 0)
        .then((r) => {
          if (!r.ok) {
            // Server restarted or run expired — clear stale state.
            localStorage.removeItem(RUN_ID_KEY);
            localStorage.removeItem(RUN_STATUS_KEY);
            setRunId(null);
            setRunStatus("idle");
            return null;
          }
          return r.json();
        })
        .then((json) => {
          if (!json) return;
          if (Array.isArray(json.lines) && json.lines.length > 0) {
            setLogs(json.lines);
            setNextOffset(json.next_offset ?? json.lines.length);
          }
          if (json.status) setRunStatus(json.status);
        })
        .catch(() => {
          // Server unreachable — clear stale state.
          localStorage.removeItem(RUN_ID_KEY);
          localStorage.removeItem(RUN_STATUS_KEY);
          setRunId(null);
          setRunStatus("idle");
        });
    }
    // Intentionally run once on mount only.
  }, []);

  // Persist run ID to localStorage so logs survive page refresh.
  useEffect(() => {
    if (runId) {
      localStorage.setItem(RUN_ID_KEY, runId);
      localStorage.setItem(RUN_STATUS_KEY, runStatus);
    }
  }, [runId, runStatus]);

  // 800ms log polling loop.
  useEffect(() => {
    if (!runId || runStatus === "completed" || runStatus === "error" || runStatus === "stopped") {
      return;
    }
    const timer = window.setInterval(async () => {
      try {
        const logRes = await api.getRunLogs(runId, nextOffset);
        if (!logRes.ok) return;
        const logJson = await logRes.json();
        setRunStatus(logJson.status ?? runStatus);
        if (Array.isArray(logJson.lines) && logJson.lines.length > 0) {
          setLogs((prev) => [...prev, ...logJson.lines]);
          setNextOffset(logJson.next_offset ?? nextOffset);
        }
        if (logJson.status === "completed" || logJson.status === "stopped") {
          const reportsRes = await api.getRunReports(runId);
          if (reportsRes.ok) {
            const reportsJson = await reportsRes.json();
            setReportLinks(reportsJson.reports ?? []);
          }
        }
      } catch {
        // Network error — keep polling, server may recover.
      }
    }, 800);
    return () => window.clearInterval(timer);
  }, [runId, runStatus, nextOffset]);

  async function startRun(payload: StartRunPayload) {
    setActionInProgress(true);
    setLogs([]);
    setNextOffset(0);
    setReportLinks([]);
    setRunStatus("queued");
    onError(null);
    try {
      const res = await api.startRun(payload);
      if (!res.ok) {
        setRunStatus("error");
        onError(`Server returned ${res.status}: ${res.statusText}`);
        return;
      }
      const json = await res.json();
      setRunId(json.id);
      setRunStatus(json.status);
    } catch {
      setRunStatus("error");
      onError("Failed to start diagnostic run. Is the server reachable?");
    } finally {
      setActionInProgress(false);
    }
  }

  async function stopRun() {
    if (!runId) return;
    setActionInProgress(true);
    try {
      const res = await api.stopRun(runId);
      if (res.ok) {
        setRunStatus("stopped");
      } else {
        onError(`Failed to stop run: ${res.statusText}`);
      }
    } catch {
      onError("Failed to stop diagnostic run.");
    } finally {
      setActionInProgress(false);
    }
  }

  return {
    runId,
    runStatus,
    isRunning,
    logs,
    setLogs,
    reportLinks,
    elapsedSec,
    secSinceLastLog,
    actionInProgress,
    startRun,
    stopRun
  };
}
