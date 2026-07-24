import { useEffect, useMemo, useRef, useState } from "react";
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
import { CameraAssignment, Device, PageId, Profile, TestDefinition, TestSummary, TriggerMode } from "./types";

const GROUP_SELECTORS = ["all", "implemented", "stable"];

export default function App() {
  const [devices, setDevices] = useState<Device[]>([]);
  const [profiles, setProfiles] = useState<Profile[]>([]);
  const [tests, setTests] = useState<TestDefinition[]>([]);
  const [selectedCameras, setSelectedCameras] = useState<string[]>([]);
  const [triggerMode, setTriggerMode] = useState<TriggerMode>("free-run");
  const [assignmentMode, setAssignmentMode] = useState<"single" | "per-camera">("single");
  const [singleProfileId, setSingleProfileId] = useState("");
  const [cameraAssignments, setCameraAssignments] = useState<CameraAssignment[]>([]);
  const [backends, setBackends] = useState(["mmap"]);
  const [selectedTests, setSelectedTests] = useState(["implemented"]);
  const [reports, setReports] = useState(["json", "html"]);
  const [includeLong, setIncludeLong] = useState(false);
  const [includeExperimental, setIncludeExperimental] = useState(false);
  const [selectedThresholdId, setSelectedThresholdId] = useState("default");
  const [severityFilter, setSeverityFilter] = useState("all");
  const [autoScroll, setAutoScroll] = useState(true);
  const [activePage, setActivePage] = useState<PageId>("dashboard");
  const [viewedRunId, setViewedRunId] = useState<string | null>(null);
  const [errorMessage, setErrorMessage] = useState<string | null>(null);
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
  } = useRunPolling(setErrorMessage);

  async function loadBasics() {
    try {
      const [deviceRes, profileRes, testRes] = await Promise.all([
        api.getDevices(),
        api.getProfiles(),
        api.getTests()
      ]);
      if (!deviceRes.ok || !profileRes.ok || !testRes.ok) {
        setErrorMessage("Failed to load device metadata from server.");
        return;
      }
      const deviceJson = await deviceRes.json();
      const profileJson = await profileRes.json();
      const testJson = await testRes.json();
      setDevices(deviceJson.devices ?? []);
      setProfiles(profileJson.profiles ?? []);
      setTests(testJson.tests ?? []);
      setErrorMessage(null);
    } catch {
      setErrorMessage("Cannot connect to diagnostic server. Is it running?");
    }
  }

  useEffect(() => {
    loadBasics();
    // Load once on mount; loadBasics is stable enough for this purpose.
  }, []);

  useEffect(() => {
    if (singleProfileId && profiles.some((profile) => profile.id === singleProfileId)) return;
    setSingleProfileId(profiles[0]?.id ?? "");
  }, [profiles, singleProfileId]);

  useEffect(() => {
    setCameraAssignments((current) =>
      selectedCameras.map((path) => current.find((assignment) => assignment.path === path) ?? {
        path,
        profile_id: "",
        trigger_channel_id: ""
      })
    );
  }, [selectedCameras]);

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
      if (!groups.has(test.category)) {
        groups.set(test.category, []);
      }
      groups.get(test.category)!.push(test);
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

  /* Returns true when a test is covered by the current selector state. */
  function isTestSelected(test: TestDefinition): boolean {
    if (selectedTests.includes("all")) return true;
    if (selectedTests.includes("implemented") && test.implemented_in_core) return true;
    if (selectedTests.includes("stable") && !test.risky) return true;
    return selectedTests.includes(test.id);
  }

  /* Toggle an individual test. Drops group selectors and works with explicit IDs. */
  function toggleTest(testId: string) {
    const hasGroup = selectedTests.some((s) => GROUP_SELECTORS.includes(s));
    if (hasGroup) {
      // Expand the group to explicit IDs first, then toggle.
      const expanded = tests.map((t) => t.id).filter((id) => isTestSelected(tests.find((t) => t.id === id)!));
      const next = expanded.includes(testId) ? expanded.filter((id) => id !== testId) : [...expanded, testId];
      setSelectedTests(next);
    } else {
      toggleListValue(testId, selectedTests, setSelectedTests);
    }
  }

  function setTestGroupSelector(selector: string) {
    setSelectedTests([selector]);
  }

  function toggleTheme() {
    const next: ThemeMode = theme === "dark" ? "light" : "dark";
    setTheme(next);
    applyTheme(next);
  }

  function requestStart() {
    if (selectedCameras.length === 0) {
      setErrorMessage("Select at least one camera before starting a run.");
      return;
    }
    if (triggerMode !== "free-run" && cameraAssignments.some((assignment) => !assignment.profile_id || !assignment.trigger_channel_id)) {
      setErrorMessage("Route every selected camera to a compatible trigger channel.");
      setActivePage("profiles");
      return;
    }
    requestConfirm({
      title: "Start Diagnostic",
      message: `Run diagnostics on ${selectedCameras.length} camera(s) in ${triggerMode} mode?`,
      confirmLabel: "Start",
      variant: "primary",
      onConfirm: () => {
        closeConfirm();
        setViewedRunId(null);
        startRun({
          trigger_mode: triggerMode,
          cameras: cameraAssignments,
          memory_backends: backends,
          test_selectors: selectedTests.length ? selectedTests : ["implemented"],
          report_formats: reports,
          run_mode: selectedCameras.length > 1 ? "parallel" : "sequential",
          include_long_tests: includeLong,
          include_experimental_tests: includeExperimental,
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
            selectedCameras={selectedCameras}
            onToggle={(path) => toggleListValue(path, selectedCameras, setSelectedCameras)}
            onRefresh={loadBasics}
          />
        )}

        {activePage === "profiles" && (
          <ProfileSelectionPage
            devices={devices.filter((device) => selectedCameras.includes(device.path))}
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
            onError={setErrorMessage}
            backends={backends}
            onToggleBackend={(backend) => toggleListValue(backend, backends, setBackends)}
          />
        )}

        {activePage === "tests" && (
          <TestSelectionPage
            groupedTests={groupedTests}
            selectedTests={selectedTests}
            isTestSelected={isTestSelected}
            onSetGroupSelector={setTestGroupSelector}
            onToggleTest={toggleTest}
            includeLong={includeLong}
            onIncludeLongChange={setIncludeLong}
            includeExperimental={includeExperimental}
            onIncludeExperimentalChange={setIncludeExperimental}
            triggerMode={triggerMode}
          />
        )}

        {activePage === "config" && (
          <ThresholdConfigPage
            selectedThresholdId={selectedThresholdId}
            onSelectedChange={setSelectedThresholdId}
            onError={setErrorMessage}
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
            liveRunStatus={runStatus}
          />
        )}
      </section>

      <Toast message={errorMessage} onDismiss={() => setErrorMessage(null)} />
      <ConfirmDialog dialog={confirmDialog} onClose={closeConfirm} />
    </main>
  );
}
