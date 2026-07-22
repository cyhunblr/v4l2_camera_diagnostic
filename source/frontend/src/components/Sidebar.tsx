import React from "react";
import { Camera, FileDown, Gauge, History, ListChecks, Play, SlidersHorizontal, Square, Terminal, Settings2 } from "lucide-react";
import { PageId } from "../types";
import { ThemeMode } from "../theme";
import { ThemeToggle } from "./ThemeToggle";
import logoMark from "../assets/logo-mark.png";

type NavItem = { id: PageId; label: string; icon: React.ReactNode };

const CONFIGURE_ITEMS: NavItem[] = [
  { id: "cameras", label: "Cameras", icon: <Camera size={16} /> },
  { id: "profiles", label: "Profiles", icon: <SlidersHorizontal size={16} /> },
  { id: "tests", label: "Tests", icon: <ListChecks size={16} /> },
  { id: "thresholds", label: "Thresholds", icon: <Settings2 size={16} /> },
  { id: "reports", label: "Report Formats", icon: <FileDown size={16} /> }
];

type Props = {
  activePage: PageId;
  onNavigate: (page: PageId) => void;
  isRunning: boolean;
  runStatus: string;
  actionInProgress: boolean;
  onRequestStart: () => void;
  onRequestStop: () => void;
  theme: ThemeMode;
  onToggleTheme: () => void;
};

export function Sidebar({
  activePage,
  onNavigate,
  isRunning,
  runStatus,
  actionInProgress,
  onRequestStart,
  onRequestStop,
  theme,
  onToggleTheme
}: Props) {
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
        {CONFIGURE_ITEMS.map((item) => (
          <button type="button" key={item.id} className={activePage === item.id ? "active" : ""} onClick={() => onNavigate(item.id)}>
            {item.icon} {item.label}
          </button>
        ))}

        <div className="nav-divider" />
        <button type="button" className={activePage === "output" ? "active" : ""} onClick={() => onNavigate("output")}>
          <Terminal size={16} /> Live Output
        </button>

        <div className="nav-divider" />
        <button type="button" className={activePage === "results" ? "active" : ""} onClick={() => onNavigate("results")}>
          <History size={16} /> Results
        </button>
      </nav>

      <select
        className="mobile-navigation"
        aria-label="Navigate"
        value={activePage}
        onChange={(event) => onNavigate(event.target.value as PageId)}
      >
        <option value="dashboard">Dashboard</option>
        {CONFIGURE_ITEMS.map((item) => <option key={item.id} value={item.id}>{item.label}</option>)}
        <option value="output">Live Output</option>
        <option value="results">Results</option>
      </select>

      <div className="sidebar-run-controls">
        <div className="sidebar-run-status">
          <span className={`status-dot ${isRunning ? "running" : ""}`} />
          <span>{runStatus}</span>
        </div>
        <button
          className={`run-button ${isRunning ? "stop" : ""}`}
          onClick={isRunning ? onRequestStop : onRequestStart}
          disabled={actionInProgress}
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
