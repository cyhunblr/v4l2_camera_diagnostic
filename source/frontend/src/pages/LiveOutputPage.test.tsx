import { createRef } from "react";
import { describe, expect, it, vi } from "vitest";
import { render, screen, fireEvent } from "@testing-library/react";
import { LiveOutputPage } from "./LiveOutputPage";
import { LogLine } from "../types";

const mockLogs: LogLine[] = [
  {
    offset: 1,
    severity: "info",
    message: "Initializing V4L2 diagnostic engine",
    timestamp_utc: "2026-08-05T10:00:00.000Z",
    log_type: "progress"
  },
  {
    offset: 2,
    severity: "warn",
    message: "Buffer queue depth near capacity",
    timestamp_utc: "2026-08-05T10:00:01.500Z",
    log_type: "progress"
  }
];

describe("LiveOutputPage (Faz 4.7)", () => {
  it("renders log timestamps and messages", () => {
    const outputRef = createRef<HTMLDivElement>();
    render(
      <LiveOutputPage
        logs={mockLogs}
        visibleLogs={mockLogs}
        severityFilter="all"
        onSeverityFilterChange={vi.fn()}
        autoScroll={true}
        onAutoScrollChange={vi.fn()}
        onClearLogs={vi.fn()}
        isRunning={false}
        runStatus="completed"
        elapsedSec={10}
        secSinceLastLog={0}
        outputRef={outputRef}
      />
    );

    expect(screen.getByText("10:00:00.000")).toBeInTheDocument();
    expect(screen.getByText("Initializing V4L2 diagnostic engine")).toBeInTheDocument();
  });

  it("limits displayed logs to last 1000 entries (buffer windowing)", () => {
    const outputRef = createRef<HTMLDivElement>();
    const manyLogs: LogLine[] = Array.from({ length: 1200 }, (_, i) => ({
      offset: i + 1,
      severity: "info",
      message: `Log line number ${i + 1}`,
      timestamp_utc: "2026-08-05T10:00:00Z",
      log_type: "progress"
    }));

    render(
      <LiveOutputPage
        logs={manyLogs}
        visibleLogs={manyLogs}
        severityFilter="all"
        onSeverityFilterChange={vi.fn()}
        autoScroll={true}
        onAutoScrollChange={vi.fn()}
        onClearLogs={vi.fn()}
        isRunning={true}
        runStatus="running"
        elapsedSec={5}
        secSinceLastLog={0}
        outputRef={outputRef}
      />
    );

    // Line 1 should be windowed out (only lines 201 to 1200 rendered)
    expect(screen.queryByText("Log line number 1")).not.toBeInTheDocument();
    expect(screen.getByText("Log line number 1200")).toBeInTheDocument();
  });

  it("renders export logs button and handles click", () => {
    const outputRef = createRef<HTMLDivElement>();
    const createObjectURLMock = vi.fn().mockReturnValue("blob:test");
    const revokeObjectURLMock = vi.fn();
    vi.stubGlobal("URL", { createObjectURL: createObjectURLMock, revokeObjectURL: revokeObjectURLMock });

    render(
      <LiveOutputPage
        logs={mockLogs}
        visibleLogs={mockLogs}
        severityFilter="all"
        onSeverityFilterChange={vi.fn()}
        autoScroll={true}
        onAutoScrollChange={vi.fn()}
        onClearLogs={vi.fn()}
        isRunning={false}
        runStatus="completed"
        elapsedSec={10}
        secSinceLastLog={0}
        outputRef={outputRef}
      />
    );

    const exportBtn = screen.getByRole("button", { name: "Export logs as text" });
    expect(exportBtn).toBeInTheDocument();
    expect(exportBtn).not.toBeDisabled();

    fireEvent.click(exportBtn);
    expect(createObjectURLMock).toHaveBeenCalled();
    expect(revokeObjectURLMock).toHaveBeenCalled();

    vi.unstubAllGlobals();
  });
});
