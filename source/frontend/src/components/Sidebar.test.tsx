import { describe, expect, it, vi } from "vitest";
import { render, screen, within } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { Sidebar } from "./Sidebar";
import { PageId } from "../types";

const ALL_PAGES: PageId[] = ["dashboard", "cameras", "profiles", "tests", "config", "output", "results"];

/**
 * The Configure group's visible step labels, in render order. The buttons are
 * siblings of the group label, not children of a wrapper, so walk forward until the
 * next divider.
 */
function configureLabels(): string[] {
  const groups = [...document.querySelectorAll(".nav-group-label")];
  const configure = groups.find((g) => g.textContent === "Configure");
  const out: string[] = [];
  let node = configure?.nextElementSibling ?? null;
  while (node && !node.classList.contains("nav-divider")) {
    if (node.tagName === "BUTTON") out.push(node.textContent!.trim());
    node = node.nextElementSibling;
  }
  return out;
}

function renderSidebar(over: Partial<Parameters<typeof Sidebar>[0]> = {}) {
  const onNavigate = vi.fn();
  const props = {
    activePage: "dashboard" as PageId,
    onNavigate,
    navigationAvailability: new Map(ALL_PAGES.map((p) => [p, true])),
    isRunning: false,
    runStatus: "Idle",
    actionInProgress: false,
    canStartDiagnostic: true,
    onRequestStart: vi.fn(),
    onRequestStop: vi.fn(),
    theme: "dark" as const,
    onToggleTheme: vi.fn(),
    ...over
  };
  render(<Sidebar {...props} />);
  return props;
}

describe("Sidebar", () => {
  // The nav buttons and the mobile <select> were once maintained as two
  // hand-written lists and silently drifted apart. Both must stay derived
  // from the same item arrays.
  it("offers the same destinations in the nav and the mobile select", () => {
    renderSidebar();
    const navLabels = within(screen.getByRole("navigation", { name: /primary/i }))
      .getAllByRole("button")
      .map((b) => b.textContent?.trim());
    const optionLabels = within(screen.getByRole("combobox", { name: "Navigate" }))
      .getAllByRole("option")
      .map((o) => o.textContent?.trim());

    expect(optionLabels).toEqual(navLabels);
  });

  it("groups Live Output and Result Output under Output", () => {
    renderSidebar();
    expect(screen.getByText("Configure")).toBeInTheDocument();
    expect(screen.getByText("Output")).toBeInTheDocument();
    expect(screen.getByRole("button", { name: /Live Output/ })).toBeInTheDocument();
    expect(screen.getByRole("button", { name: /Result Output/ })).toBeInTheDocument();
  });

  it("locks unreachable pages in both surfaces with the same reason", () => {
    renderSidebar({
      navigationAvailability: new Map(ALL_PAGES.map((p) => [p, p !== "results"]))
    });
    const button = screen.getByRole("button", { name: /Result Output/ });
    expect(button).toBeDisabled();
    expect(button).toHaveAttribute("title", "Results are available after the run finishes.");

    const option = within(screen.getByRole("combobox", { name: "Navigate" }))
      .getByRole("option", { name: "Result Output" });
    expect(option).toBeDisabled();
  });

  it("navigates on click", async () => {
    const user = userEvent.setup();
    const { onNavigate } = renderSidebar();
    await user.click(screen.getByRole("button", { name: /Cameras/ }));
    expect(onNavigate).toHaveBeenCalledWith("cameras");
  });

  it("swaps Start for Stop while a run is in flight", () => {
    renderSidebar({ isRunning: true, runStatus: "Running" });
    expect(screen.getByRole("button", { name: /Stop Diagnostic/ })).toBeEnabled();
    expect(screen.queryByRole("button", { name: /Start Diagnostic/ })).not.toBeInTheDocument();
  });

  it("blocks Start until the setup flow is complete and says why", () => {
    renderSidebar({ canStartDiagnostic: false });
    const start = screen.getByRole("button", { name: /Start Diagnostic/ });
    expect(start).toBeDisabled();
    expect(start).toHaveAttribute("title", "Complete the setup flow before starting diagnostics.");
  });
});

// --- Report Formats removed (plan 2.10) -----------------------------------
//
// The user no longer chooses formats: every run writes HTML, JSON and Markdown. The
// Configure step and its nav entry go with the choice. Result Output stays.
//
// Also a decision-conformance guard (protocol P8): if Report Formats is ever added
// back to the navigation, this fails.
describe("Report Formats navigation is gone", () => {
  it("offers no Report Formats entry", () => {
    renderSidebar();

    expect(screen.queryByRole("button", { name: /Report Formats/i })).not.toBeInTheDocument();
    expect(screen.queryByText(/Report Formats/i)).not.toBeInTheDocument();
  });

  it("keeps the Configure steps that remain, in order", () => {
    renderSidebar();

    const configure = configureLabels();
    expect(configure).toEqual(["Cameras", "Profiles", "Test Selection", "Test Configuration"]);
    expect(configure).not.toContain("Report Formats");
  });

  it("offers no Report Formats option in the mobile select either", () => {
    renderSidebar();

    const options = [...screen.getByLabelText("Navigate").querySelectorAll("option")].map((o) => o.textContent);
    expect(options).not.toContain("Report Formats");
    // The mobile select mirrors the same item lists, so this also proves the desktop
    // nav and the select cannot drift apart.
    expect(options).toEqual(["Dashboard", "Cameras", "Profiles", "Test Selection", "Test Configuration",
                             "Live Output", "Result Output"]);
  });

  it("keeps Result Output", () => {
    renderSidebar();

    expect(screen.getByRole("button", { name: /Result Output/i })).toBeInTheDocument();
    expect([...screen.getByLabelText("Navigate").querySelectorAll("option")].map((o) => o.textContent))
      .toContain("Result Output");
  });
});
