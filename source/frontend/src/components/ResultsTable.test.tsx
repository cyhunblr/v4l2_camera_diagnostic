import { describe, expect, it } from "vitest";
import { render, screen } from "@testing-library/react";
import { ResultsTable } from "./ResultsTable";
import { TestSummary } from "../types";

const summary = (over: Partial<TestSummary> = {}): TestSummary => ({
  test: "t01-device-compliance",
  status: "pass",
  message: "Device is compliant",
  camera: "/dev/video0",
  ...over
});

describe("ResultsTable", () => {
  it("shows the empty message instead of a headerless table", () => {
    render(<ResultsTable summaries={[]} />);
    expect(screen.getByText(/No test results yet/)).toBeInTheDocument();
    expect(screen.queryByRole("table")).not.toBeInTheDocument();
  });

  it("renders one row per summary and keeps the row order", () => {
    render(<ResultsTable summaries={[summary({ test: "t01" }), summary({ test: "t02" })]} />);
    const cells = screen.getAllByRole("cell").filter((c) => c.classList.contains("col-test"));
    expect(cells.map((c) => c.textContent)).toEqual(["t01", "t02"]);
  });

  // Regression guard: the table used to string-match English messages to drop a
  // second "Completed in Nms" line per test. The backend now emits exactly one
  // verdict line, so every summary must survive — including repeated messages.
  it("does not de-duplicate summaries that share a message", () => {
    render(
      <ResultsTable
        summaries={[
          summary({ test: "t01", message: "Completed" }),
          summary({ test: "t02", message: "Completed" })
        ]}
      />
    );
    expect(screen.getAllByRole("row")).toHaveLength(3); // header + two results
  });

  // Characterisation, not a contract: these are today's labels. Faz 4.0 moves
  // test status onto PASS / WARN / FAIL / SKIP (distribution counts keep
  // Passed / Warned / Failed / Skipped), and this expectation changes with it.
  // See plan item 4.0 in docs/web-ui-audit-fix-plan.md.
  it("maps every backend status value to a label (pre-4.0 wording)", () => {
    render(
      <ResultsTable
        summaries={[
          summary({ test: "a", status: "pass" }),
          summary({ test: "b", status: "fail" }),
          summary({ test: "c", status: "warn" }),
          summary({ test: "d", status: "skipped" })
        ]}
      />
    );
    for (const label of ["Pass", "Fail", "Warn", "Skipped"]) {
      expect(screen.getByText(label)).toBeInTheDocument();
    }
  });

  it("never leaves a status cell empty", () => {
    render(<ResultsTable summaries={[summary({ status: "" })]} />);
    const cell = screen.getAllByRole("cell").find((c) => c.classList.contains("col-status"));
    expect(cell?.textContent?.trim()).not.toBe("");
  });

  it("tags each row with its status so CSS can colour it", () => {
    const { container } = render(<ResultsTable summaries={[summary({ status: "WARN" })]} />);
    expect(container.querySelector("tr.status-warn")).not.toBeNull();
  });

  it("strips the leading check mark the backend prefixes to verdicts", () => {
    render(<ResultsTable summaries={[summary({ message: "✓ Device is compliant" })]} />);
    expect(screen.getByText("Device is compliant")).toBeInTheDocument();
  });
});
