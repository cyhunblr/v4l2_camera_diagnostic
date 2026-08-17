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
import { LiveOutputPage } from "./pages/LiveOutputPage";
import { ResultsPage } from "./pages/ResultsPage";
import {
  CameraAssignment,
  Device,
  MASTER_ROLE,
  PageId,
  Profile,
  TestDefinition,
  TestSummary,
  TriggerMode,
  expectedRoles,
  groupTestsByLayer,
  slaveRole,
  summariesFromResult
} from "./types";

// No "reports" step: v5 removed the format choice, every run writes HTML, JSON and
// Markdown (plan 2.10).
const CONFIGURE_FLOW: PageId[] = ["cameras", "profiles", "tests", "config"];
const TERMINAL_RUN_STATUSES = new Set(["completed", "stopped", "error"]);


type ToastState = {
  message: string;
  tone: "error" | "success" | "info";
};

export default function App() {
  const [devices, setDevices] = useState<Device[]>([]);
  const [profiles, setProfiles] = useState<Profile[]>([]);
  // The schema version the backend writes, taken from its own response. A second
  // constant in the frontend would have to be found and bumped alongside the
  // backend's, and a missed one breaks profile creation outright.
  const [profileSchemaVersion, setProfileSchemaVersion] = useState<number | null>(null);
  const [tests, setTests] = useState<TestDefinition[]>(api.DEFAULT_TESTS);
  const [cameraMode, setCameraMode] = useState<"single" | "multi">("single");
  const [masterPath, setMasterPath] = useState<string | null>(null);
  const [slavePaths, setSlavePaths] = useState<string[]>([]);
  const [triggerMode, setTriggerMode] = useState<TriggerMode>("free-run");
  const [singleProfileId, setSingleProfileId] = useState("");
  const [cameraAssignments, setCameraAssignments] = useState<CameraAssignment[]>([]);
  const [backends, setBackends] = useState(["mmap"]);
  const [selectedTests, setSelectedTests] = useState<string[]>([]);
  const [activeTags, setActiveTags] = useState<string[]>(["stable"]);
  const [activeAction, setActiveAction] = useState<"select-all" | "clear-all" | "reset-stable">("reset-stable");
  const [selectedThresholdId, setSelectedThresholdId] = useState("default");
  const [thresholdDirty, setThresholdDirty] = useState(false);
  const [severityFilter, setSeverityFilter] = useState("all");
  const [autoScroll, setAutoScroll] = useState(true);
  const [activePage, setActivePage] = useState<PageId>("dashboard");
  const [unlockedPages, setUnlockedPages] = useState<Set<PageId>>(() => new Set(["dashboard"]));
  const [setupComplete, setSetupComplete] = useState(false);
  const [viewedRunId, setViewedRunId] = useState<string | null>(null);
  const [toast, setToast] = useState<ToastState | null>(null);
  const [theme, setTheme] = useState<ThemeMode>(getInitialTheme());
  const outputRef = useRef<HTMLDivElement | null>(null);

  const { confirmDialog, requestConfirm, closeConfirm } = useConfirmDialog();
  const {
    runId,
    runStatus,
    isRunning,
    logs,
    setLogs,
    liveResult,
    reportLinks,
    elapsedSec,
    secSinceLastLog,
    actionInProgress,
    startRun,
    stopRun
  } = useRunPolling(showError);
  const previousRunStatusRef = useRef(runStatus);

  function showError(message: string | null) {
    setToast(message ? { message, tone: "error" } : null);
  }

  function showSuccess(message: string) {
    setToast({ message, tone: "success" });
  }

  const [devicesLoading, setDevicesLoading] = useState(true);
  const [devicesError, setDevicesError] = useState<string | null>(null);

  const loadBasics = useCallback(async () => {
    setDevicesLoading(true);
    setDevicesError(null);
    try {
      const [deviceRes, profileRes, testRes] = await Promise.all([
        api.getDevices(),
        api.getProfiles(),
        api.getTests()
      ]);
      if (!deviceRes.ok || !profileRes.ok || !testRes.ok) {
        const msg = "Failed to load device metadata from server.";
        showError(msg);
        setDevicesError(msg);
        return;
      }
      const deviceJson = await deviceRes.json();
      const profileJson = await profileRes.json();
      const testJson = await testRes.json();
      setDevices(deviceJson.devices ?? []);
      setProfiles(profileJson.profiles ?? []);
      setProfileSchemaVersion(
        typeof profileJson.schema_version === "number" ? profileJson.schema_version : null
      );
      setTests(testJson.tests ?? []);
      showError(null);
      setDevicesError(null);
    } catch {
      const msg = "Cannot connect to diagnostic server. Is it running?";
      showError(msg);
      setDevicesError(msg);
    } finally {
      setDevicesLoading(false);
    }
  }, []);

  useEffect(() => {
    loadBasics();
  }, [loadBasics]);

  useEffect(() => {
    const validCapturePaths = new Set(
      devices.filter((d) => d.supports_capture).map((d) => d.path)
    );
    if (masterPath && !validCapturePaths.has(masterPath)) {
      setMasterPath(null);
    }
    const filteredSlaves = slavePaths.filter((path) => validCapturePaths.has(path));
    if (filteredSlaves.length !== slavePaths.length) {
      setSlavePaths(filteredSlaves);
    }
  }, [devices, masterPath, slavePaths]);

  // Guard: clear profile selection if the profile was deleted.
  useEffect(() => {
    if (singleProfileId && !profiles.some((profile) => profile.id === singleProfileId)) {
      setSingleProfileId("");
    }
  }, [profiles, singleProfileId]);

  // Pre-fill trigger mode and backends from profile.defaults when the user
  // explicitly picks a new profile. Runs only when singleProfileId changes so
  // a background refresh of the profiles list does not clobber user edits.
  const prevSingleProfileIdRef = useRef("");
  useEffect(() => {
    if (singleProfileId && singleProfileId !== prevSingleProfileIdRef.current) {
      prevSingleProfileIdRef.current = singleProfileId;
      const profile = profiles.find((p) => p.id === singleProfileId);
      if (profile?.defaults) {
        if (profile.defaults.trigger_mode) setTriggerMode(profile.defaults.trigger_mode);
        if (profile.defaults.memory_backends && profile.defaults.memory_backends.length > 0) {
          setBackends(profile.defaults.memory_backends);
        }
      }
    } else if (!singleProfileId) {
      prevSingleProfileIdRef.current = "";
    }
  }, [singleProfileId, profiles]);

  const involvedPaths = useMemo(
    () => [masterPath, ...slavePaths].filter((path): path is string => Boolean(path)),
    [masterPath, slavePaths]
  );

  const unlockPage = useCallback((page: PageId) => {
    setUnlockedPages((current) => {
      if (current.has(page)) return current;
      const next = new Set(current);
      next.add(page);
      return next;
    });
  }, []);

  const resetDiagnosticFlow = useCallback(() => {
    setViewedRunId(null);
    setSetupComplete(false);
    setUnlockedPages(new Set<PageId>(["dashboard", "cameras"]));
    setActivePage("cameras");
  }, []);

  // Roles come from the run topology, not from the user: master first, then
  // slave-1..slave-N in slavePaths order. A camera moving to another node keeps
  // its role, which is the point of v4's role-based routing.
  useEffect(() => {
    setCameraAssignments(
      involvedPaths.map((path, index) => ({
        path,
        role: index === 0 ? MASTER_ROLE : slaveRole(index - 1)
      }))
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

  const groupedTests = useMemo(() => groupTestsByLayer(tests), [tests]);

  const visibleLogs = useMemo(
    () => logs.filter((line) => severityFilter === "all" || line.severity === severityFilter),
    [logs, severityFilter]
  );

  const assignmentsReady = useMemo(() => {
    if (!masterPath) return false;
    // Free-run does not route at all. Otherwise the run needs a Trigger Profile
    // whose role_bindings cover this topology; the backend is the authority on
    // whether they do (resolve_role_bindings), so the UI only checks that one was
    // chosen rather than re-deriving the rule.
    if (triggerMode === "free-run") return true;
    if (!singleProfileId) return false;
    const profile = profiles.find((item) => item.id === singleProfileId);
    if (!profile) return false;
    const needed = expectedRoles(involvedPaths.length - 1);
    return needed.every((role: string) => profile.role_bindings.some((binding) => binding.role === role));
  }, [involvedPaths, masterPath, profiles, singleProfileId, triggerMode]);

  const testsReady = selectedTests.length > 0;
  const configReady = Boolean(selectedThresholdId);
  const canStartDiagnostic = setupComplete && Boolean(masterPath) && assignmentsReady && testsReady && configReady;

  const navigationAvailability = useMemo(() => {
    const available = new Map<PageId, boolean>();
    (["dashboard", ...CONFIGURE_FLOW, "output", "results"] as PageId[]).forEach((page) => {
      available.set(page, page === "dashboard" || unlockedPages.has(page));
    });
    return available;
  }, [unlockedPages]);

  useEffect(() => {
    if (masterPath) unlockPage("profiles");
  }, [masterPath, unlockPage]);

  useEffect(() => {
    if (unlockedPages.has("profiles") && assignmentsReady && (activePage === "profiles" || unlockedPages.has("tests"))) {
      unlockPage("tests");
    }
  }, [activePage, assignmentsReady, unlockPage, unlockedPages]);

  useEffect(() => {
    if (unlockedPages.has("tests") && testsReady && (activePage === "tests" || unlockedPages.has("config"))) {
      unlockPage("config");
    }
  }, [activePage, testsReady, unlockPage, unlockedPages]);

  useEffect(() => {
    if (unlockedPages.has("config") && configReady) {
      // Test Configuration is the last Configure step now, so completing it
      // completes the setup flow.
      setSetupComplete(true);
    }
  }, [activePage, configReady, unlockPage, unlockedPages]);

  useEffect(() => {
    if (logs.length > 0) unlockPage("output");
  }, [logs.length, unlockPage]);

  useEffect(() => {
    const previous = previousRunStatusRef.current;
    const wasRunning = previous === "queued" || previous === "running";
    const isTerminal = TERMINAL_RUN_STATUSES.has(runStatus);
    // Only a run that finished in this session unlocks Result Output. A run
    // restored from localStorage by useRunPolling arrives already terminal, and
    // unlocking on that alone made Result Output clickable on a fresh page load
    // before the user had done anything. Opening a run from Dashboard history
    // unlocks it through its own path (openHistoricalRun).
    if (wasRunning && isTerminal && runId) {
      unlockPage("results");
      setActivePage("results");
    }
    previousRunStatusRef.current = runStatus;
  }, [runId, runStatus, unlockPage]);

  // Derive completed test summary from summary-type log lines.
  // From the structured result, never from log text: a human-readable message is not a
  // data source for a status (plan 2.6.1). This replaced a regex over summary lines
  // that was duplicated byte-for-byte in ResultsPage.
  const testSummaries: TestSummary[] = useMemo(() => summariesFromResult(liveResult), [liveResult]);

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

  function navigateTo(page: PageId) {
    if (!navigationAvailability.get(page)) {
      showError("Complete the previous diagnostic step before opening this section.");
      return;
    }
    if (thresholdDirty && activePage === "config" && page !== "config") {
      requestConfirm({
        title: "Unsaved Changes",
        message:
          "You have unsaved changes in Test Configuration. Leave anyway?",
        confirmLabel: "Leave",
        variant: "danger",
        onConfirm: () => {
          setThresholdDirty(false);
          setActivePage(page);
        }
      });
      return;
    }
    setActivePage(page);
  }

  function requestStart() {
    if (!canStartDiagnostic) {
      showError("Complete the diagnostic setup flow before starting a run.");
      return;
    }
    if (!masterPath) {
      showError("Select a camera to test before starting a run.");
      return;
    }
    if (triggerMode !== "free-run" && !singleProfileId) {
      showError("Select a Trigger Profile for the run.");
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
          // One run-level Trigger Profile; free-run carries none.
          trigger_profile_id: triggerMode === "free-run" ? "" : singleProfileId,
          master,
          slaves,
          memory_backends: backends,
          test_selectors: selectedTests.length ? selectedTests : ["stable"],
          threshold_config_id: selectedThresholdId
        });
        unlockPage("output");
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
    unlockPage("results");
    setActivePage("results");
  }

  return (
    <main className="app-shell">
      <Sidebar
        activePage={activePage}
        onNavigate={navigateTo}
        navigationAvailability={navigationAvailability}
        isRunning={isRunning}
        runStatus={runStatus}
        actionInProgress={actionInProgress}
        canStartDiagnostic={canStartDiagnostic}
        onRequestStart={requestStart}
        onRequestStop={requestStop}
        theme={theme}
        onToggleTheme={toggleTheme}
      />

      <section className="workspace">
        {activePage === "dashboard" && (
          <DashboardPage
            onViewRun={handleViewRun}
            onStartNewDiagnostic={resetDiagnosticFlow}
            isRunning={isRunning}
          />
        )}

        {activePage === "cameras" && (
          <CameraSelectionPage
            devices={devices}
            loading={devicesLoading}
            error={devicesError}
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
            profileSchemaVersion={profileSchemaVersion}
            triggerMode={triggerMode}
            onTriggerModeChange={setTriggerMode}
            singleProfileId={singleProfileId}
            onSingleProfileChange={setSingleProfileId}
            assignments={cameraAssignments}
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
            activeTags={activeTags}
            setActiveTags={setActiveTags}
            activeAction={activeAction}
            setActiveAction={setActiveAction}
          />
        )}

        {activePage === "config" && (
          <ThresholdConfigPage
            selectedThresholdId={selectedThresholdId}
            onSelectedChange={setSelectedThresholdId}
            selectedTests={selectedTests}
            tests={tests}
            onDirtyChange={setThresholdDirty}
            onError={showError}
          />
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
