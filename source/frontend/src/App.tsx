import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import * as api from "./api";
import { getInitialTheme, applyTheme, ThemeMode } from "./theme";
import { useRunPolling } from "./hooks/useRunPolling";
import { useConfirmDialog } from "./hooks/useConfirmDialog";
import { Sidebar } from "./components/Sidebar";
import { Toast } from "./components/Toast";
import { ConfirmDialog } from "./components/ConfirmDialog";
import { DashboardPage } from "./pages/DashboardPage";
import { CameraSelectionPage } from "./pages/CameraSelectionPage";
import { ProfileSelectionPage } from "./pages/ProfileSelectionPage";
import { TestSelectionPage } from "./pages/TestSelectionPage";
import { ThresholdConfigPage } from "./pages/ThresholdConfigPage";
import { ReportFormatsPage } from "./pages/ReportFormatsPage";
import { LiveOutputPage } from "./pages/LiveOutputPage";
import { ResultsPage } from "./pages/ResultsPage";
import {
  CameraAssignment,
  Device,
  PageId,
  Profile,
  TestDefinition,
  TestSummary,
  TriggerMode,
  getTestLayerName
} from "./types";



type ToastState = {
  message: string;
  tone: "error" | "success" | "info";
};

export default function App() {
  const [devices, setDevices] = useState<Device[]>([]);
  const [profiles, setProfiles] = useState<Profile[]>([]);
  const [tests, setTests] = useState<TestDefinition[]>(api.DEFAULT_TESTS);
  const [cameraMode, setCameraMode] = useState<"single" | "multi">("single");
  const [masterPath, setMasterPath] = useState<string | null>(null);
  const [slavePaths, setSlavePaths] = useState<string[]>([]);
  const [triggerMode, setTriggerMode] = useState<TriggerMode>("free-run");
  const [assignmentMode, setAssignmentMode] = useState<"single" | "per-camera">("single");
  const [singleProfileId, setSingleProfileId] = useState("");
  const [cameraAssignments, setCameraAssignments] = useState<CameraAssignment[]>([]);
  const [backends, setBackends] = useState(["mmap"]);
  const [selectedTests, setSelectedTests] = useState(["implemented"]);
  const [reports, setReports] = useState(["json", "html"]);
  const [selectedThresholdId, setSelectedThresholdId] = useState("default");
  const [severityFilter, setSeverityFilter] = useState("all");
  const [autoScroll, setAutoScroll] = useState(true);
  const [activePage, setActivePage] = useState<PageId>("dashboard");
  const [viewedRunId, setViewedRunId] = useState<string | null>(null);
  const [toast, setToast] = useState<ToastState | null>(null);
  const [theme, setTheme] = useState<ThemeMode>(getInitialTheme());
  const outputRef = useRef<HTMLDivElement | null>(null);

  const { confirmDialog, requestConfirm, closeConfirm } = useConfirmDialog();
  const {
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
  } = useRunPolling(showError);

  function showError(message: string | null) {
    setToast(message ? { message, tone: "error" } : null);
  }

  function showSuccess(message: string) {
    setToast({ message, tone: "success" });
  }

  const loadBasics = useCallback(async () => {
    try {
      const [deviceRes, profileRes, testRes] = await Promise.all([
        api.getDevices(),
        api.getProfiles(),
        api.getTests()
      ]);
      if (!deviceRes.ok || !profileRes.ok || !testRes.ok) {
        showError("Failed to load device metadata from server.");
        return;
      }
      const deviceJson = await deviceRes.json();
      const profileJson = await profileRes.json();
      const testJson = await testRes.json();
      setDevices(deviceJson.devices ?? []);
      setProfiles(profileJson.profiles ?? []);
      setTests(testJson.tests ?? []);
      showError(null);
    } catch {
      showError("Cannot connect to diagnostic server. Is it running?");
    }
  }, []);

  useEffect(() => {
    loadBasics();
  }, [loadBasics]);

  useEffect(() => {
    if (singleProfileId && !profiles.some((profile) => profile.id === singleProfileId)) {
      setSingleProfileId("");
    }
  }, [profiles, singleProfileId]);

  const involvedPaths = useMemo(
    () => [masterPath, ...slavePaths].filter((path): path is string => Boolean(path)),
    [masterPath, slavePaths]
  );

  useEffect(() => {
    setCameraAssignments((current) =>
      involvedPaths.map((path) => current.find((assignment) => assignment.path === path) ?? {
        path,
        profile_id: "",
        trigger_channel_id: ""
      })
    );
  }, [involvedPaths]);

  // Switching to single-camera mode drops any slaves; changing the master
  // also removes it from the slave list so a camera can't be both.
  useEffect(() => {
    if (cameraMode === "single" && slavePaths.length > 0) {
      setSlavePaths([]);
    }
  }, [cameraMode, slavePaths.length]);

  function selectMaster(path: string) {
    setMasterPath(path);
    setSlavePaths((current) => current.filter((slave) => slave !== path));
  }

  function toggleSlave(path: string) {
    setSlavePaths((current) => (current.includes(path) ? current.filter((item) => item !== path) : [...current, path]));
  }

  useEffect(() => {
    if (autoScroll && outputRef.current) {
      outputRef.current.scrollTop = outputRef.current.scrollHeight;
    }
  }, [logs, autoScroll]);

  // Auto-scroll output view when switching to it.
  useEffect(() => {
    if (activePage === "output" && outputRef.current) {
      outputRef.current.scrollTop = outputRef.current.scrollHeight;
    }
  }, [activePage]);

  const groupedTests = useMemo(() => {
    const groups = new Map<string, TestDefinition[]>();
    for (const test of tests) {
      const layerName = getTestLayerName(test.id);
      if (!groups.has(layerName)) {
        groups.set(layerName, []);
      }
      groups.get(layerName)!.push(test);
    }
    return [...groups.entries()];
  }, [tests]);

  const visibleLogs = useMemo(
    () => logs.filter((line) => severityFilter === "all" || line.severity === severityFilter),
    [logs, severityFilter]
  );

  // Derive completed test summary from summary-type log lines.
  const testSummaries: TestSummary[] = useMemo(() => {
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
  }, [logs]);

  function toggleListValue(value: string, values: string[], setter: (values: string[]) => void) {
    setter(values.includes(value) ? values.filter((item) => item !== value) : [...values, value]);
  }

  /* Returns true when a test is selected. */
  function isTestSelected(test: TestDefinition): boolean {
    if (selectedTests.includes("all")) return true;
    return selectedTests.includes(test.id);
  }

  /* Toggle an individual test. */
  function toggleTest(testId: string) {
    toggleListValue(testId, selectedTests, setSelectedTests);
  }

  function toggleTheme() {
    const next: ThemeMode = theme === "dark" ? "light" : "dark";
    setTheme(next);
    applyTheme(next);
  }

  function requestStart() {
    if (!masterPath) {
      showError("Select a camera to test before starting a run.");
      return;
    }
    if (triggerMode !== "free-run" && cameraAssignments.some((assignment) => !assignment.profile_id || !assignment.trigger_channel_id)) {
      showError("Route every selected camera to a compatible trigger channel.");
      setActivePage("profiles");
      return;
    }
    const master = cameraAssignments.find((assignment) => assignment.path === masterPath)!;
    const slaves = cameraAssignments.filter((assignment) => slavePaths.includes(assignment.path));
    const description = slaves.length > 0 ? `1 master + ${slaves.length} slave camera(s)` : "1 camera";
    requestConfirm({
      title: "Start Diagnostic",
      message: `Run diagnostics on ${description} in ${triggerMode} mode?`,
      confirmLabel: "Start",
      variant: "primary",
      onConfirm: () => {
        closeConfirm();
        setViewedRunId(null);
        startRun({
          trigger_mode: triggerMode,
          master,
          slaves,
          memory_backends: backends,
          test_selectors: selectedTests.length ? selectedTests : ["stable"],
          report_formats: reports,
          threshold_config_id: selectedThresholdId
        });
        setActivePage("output");
      }
    });
  }

  function requestStop() {
    requestConfirm({
      title: "Stop Diagnostic",
      message: "Are you sure you want to stop the current diagnostic run? Results collected so far will be preserved.",
      confirmLabel: "Stop Run",
      variant: "danger",
      onConfirm: () => {
        closeConfirm();
        stopRun();
      }
    });
  }

  function handleViewRun(runId: string) {
    setViewedRunId(runId);
    setActivePage("results");
  }

  return (
    <main className="app-shell">
      <Sidebar
        activePage={activePage}
        onNavigate={setActivePage}
        isRunning={isRunning}
        runStatus={runStatus}
        actionInProgress={actionInProgress}
        onRequestStart={requestStart}
        onRequestStop={requestStop}
        theme={theme}
        onToggleTheme={toggleTheme}
      />

      <section className="workspace">
        {activePage === "dashboard" && <DashboardPage onViewRun={handleViewRun} />}

        {activePage === "cameras" && (
          <CameraSelectionPage
            devices={devices}
            cameraMode={cameraMode}
            onCameraModeChange={setCameraMode}
            masterPath={masterPath}
            onSelectMaster={selectMaster}
            slavePaths={slavePaths}
            onToggleSlave={toggleSlave}
            onRefresh={loadBasics}
          />
        )}

        {activePage === "profiles" && (
          <ProfileSelectionPage
            devices={devices.filter((device) => involvedPaths.includes(device.path))}
            profiles={profiles}
            triggerMode={triggerMode}
            onTriggerModeChange={setTriggerMode}
            assignmentMode={assignmentMode}
            onAssignmentModeChange={setAssignmentMode}
            singleProfileId={singleProfileId}
            onSingleProfileChange={setSingleProfileId}
            assignments={cameraAssignments}
            onAssignmentsChange={setCameraAssignments}
            onProfilesChanged={loadBasics}
            onError={showError}
            onSuccess={showSuccess}
            requestConfirm={requestConfirm}
          />
        )}

        {activePage === "tests" && (
          <TestSelectionPage
            groupedTests={groupedTests}

            setSelectedTests={setSelectedTests}
            tests={tests}
            isTestSelected={isTestSelected}
            onToggleTest={toggleTest}
            triggerMode={triggerMode}
            backends={backends}
            onToggleBackend={(backend) => toggleListValue(backend, backends, setBackends)}
          />
        )}

        {activePage === "config" && (
          <ThresholdConfigPage
            selectedThresholdId={selectedThresholdId}
            onSelectedChange={setSelectedThresholdId}
            selectedTests={selectedTests}
            onError={showError}
          />
        )}

        {activePage === "reports" && (
          <ReportFormatsPage reports={reports} onToggle={(format) => toggleListValue(format, reports, setReports)} />
        )}

        {activePage === "output" && (
          <LiveOutputPage
            logs={logs}
            visibleLogs={visibleLogs}
            severityFilter={severityFilter}
            onSeverityFilterChange={setSeverityFilter}
            autoScroll={autoScroll}
            onAutoScrollChange={setAutoScroll}
            onClearLogs={() => setLogs([])}
            isRunning={isRunning}
            runStatus={runStatus}
            elapsedSec={elapsedSec}
            secSinceLastLog={secSinceLastLog}
            actionInProgress={actionInProgress}
            onRequestStop={requestStop}
            outputRef={outputRef}
          />
        )}

        {activePage === "results" && (
          <ResultsPage
            viewedRunId={viewedRunId}
            liveSummaries={testSummaries}
            liveReportLinks={reportLinks}
          />
        )}
      </section>

      <Toast message={toast?.message ?? null} tone={toast?.tone ?? "error"} onDismiss={() => setToast(null)} />
      <ConfirmDialog dialog={confirmDialog} onClose={closeConfirm} />
    </main>
  );
}
