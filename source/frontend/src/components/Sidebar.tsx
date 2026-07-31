import React from "react";
import { Camera, FileDown, Gauge, History, ListChecks, Lock, Play, SlidersHorizontal, Square, Terminal, Settings2 } from "lucide-react";
import { PageId } from "../types";
import { ThemeMode } from "../theme";
import { ThemeToggle } from "./ThemeToggle";
import logoMark from "../assets/logo-mark.png";

type NavItem = { id: PageId; label: string; icon: React.ReactNode; lockedTitle: string };

const CONFIGURE_ITEMS: NavItem[] = [
  { id: "cameras", label: "Cameras", icon: <Camera size={16} />, lockedTitle: "Complete the previous step first." },
  { id: "profiles", label: "Profiles", icon: <SlidersHorizontal size={16} />, lockedTitle: "Complete the previous step first." },
  { id: "tests", label: "Test Selection", icon: <ListChecks size={16} />, lockedTitle: "Complete the previous step first." },
  { id: "config", label: "Test Configuration", icon: <Settings2 size={16} />, lockedTitle: "Complete the previous step first." },
  { id: "reports", label: "Report Formats", icon: <FileDown size={16} />, lockedTitle: "Complete the previous step first." }
];

const OUTPUT_ITEMS: NavItem[] = [
  {
    id: "output",
    label: "Live Output",
    icon: <Terminal size={16} />,
    lockedTitle: "Live Output opens when a diagnostic starts."
  },
  {
    id: "results",
    label: "Result Output",
    icon: <History size={16} />,
    lockedTitle: "Results are available after the run finishes."
  }
];

type Props = {
  activePage: PageId;
  onNavigate: (page: PageId) => void;
  navigationAvailability: Map<PageId, boolean>;
  isRunning: boolean;
  runStatus: string;
  actionInProgress: boolean;
  canStartDiagnostic: boolean;
  onRequestStart: () => void;
  onRequestStop: () => void;
  theme: ThemeMode;
  onToggleTheme: () => void;
};

export function Sidebar({
  activePage,
  onNavigate,
  navigationAvailability,
  isRunning,
  runStatus,
  actionInProgress,
  canStartDiagnostic,
  onRequestStart,
  onRequestStop,
  theme,
  onToggleTheme
}: Props) {
  const canNavigate = (page: PageId) => navigationAvailability.get(page) ?? false;
  const startDisabled = actionInProgress || (!isRunning && !canStartDiagnostic);
  const startTitle = !isRunning && !canStartDiagnostic ? "Complete the setup flow before starting diagnostics." : undefined;

  const renderNavItem = (item: NavItem) => {
    const disabled = !canNavigate(item.id);
    return (
      <button
        type="button"
        key={item.id}
        className={`${activePage === item.id ? "active" : ""}${disabled ? " locked" : ""}`}
        onClick={() => onNavigate(item.id)}
        disabled={disabled}
        title={disabled ? item.lockedTitle : undefined}
      >
        {item.icon} {item.label} {disabled && <Lock className="nav-lock" size={13} />}
      </button>
    );
  };

  return (
    <aside className="sidebar">
      <div className="brand">
        <span className="brand-mark">
          <img src={logoMark} alt="" />
        </span>
        <h1>V4L2 Camera Diagnostic</h1>
      </div>

      <nav aria-label="Primary navigation">
        <button type="button" className={activePage === "dashboard" ? "active" : ""} onClick={() => onNavigate("dashboard")}>
          <Gauge size={16} /> Dashboard
        </button>

        <div className="nav-divider" />
        <p className="nav-group-label">Configure</p>
        {CONFIGURE_ITEMS.map(renderNavItem)}

        <div className="nav-divider" />
        <p className="nav-group-label">Output</p>
        {OUTPUT_ITEMS.map(renderNavItem)}
      </nav>

      <select
        className="mobile-navigation"
        aria-label="Navigate"
        value={activePage}
        onChange={(event) => onNavigate(event.target.value as PageId)}
      >
        <option value="dashboard">Dashboard</option>
        {[...CONFIGURE_ITEMS, ...OUTPUT_ITEMS].map((item) => (
          <option key={item.id} value={item.id} disabled={!canNavigate(item.id)}>{item.label}</option>
        ))}
      </select>

      <div className="sidebar-run-controls">
        <div className="sidebar-run-status">
          <span className={`status-dot ${isRunning ? "running" : ""}`} />
          <span>{runStatus}</span>
        </div>
        <button
          className={`run-button ${isRunning ? "stop" : ""}`}
          onClick={isRunning ? onRequestStop : onRequestStart}
          disabled={startDisabled}
          title={startTitle}
        >
          {isRunning ? <><Square size={16} /> Stop Diagnostic</> : <><Play size={16} /> Start Diagnostic</>}
        </button>
      </div>

      <div className="sidebar-footer">
        <ThemeToggle theme={theme} onToggle={onToggleTheme} />
      </div>
    </aside>
  );
}
