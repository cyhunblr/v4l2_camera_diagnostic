// A nav entry and the page it opens must carry the SAME visible name.
//
// This is the defect it exists to lock out, as measured on 2026-08-08: five of the
// seven pages already agreed with their nav entry, but two had drifted --
//
//   nav "Cameras"  -> page heading "Camera Selection"
//   nav "Profiles" -> page heading "Trigger Routing"
//
// Neither is reachable by reading one file: the label lives in Sidebar.tsx and the
// heading in the page component, so the two can drift silently forever. Checking one
// side alone (an existing test asserted the nav list, another the headings) is exactly
// what let this survive -- the pair has to be compared.
//
// The check is on VISIBLE TEXT only. PageId values ("cameras", "profiles"), file names
// and the getProfiles() API keep their technical identifiers.

import { describe, expect, it, vi } from "vitest";
import { render, screen } from "@testing-library/react";
import { Sidebar } from "./Sidebar";
import { PageId } from "../types";
// Raw page sources: the expected label is read from the page itself at test time.
import cameraSource from "../pages/CameraSelectionPage.tsx?raw";
import dashboardSource from "../pages/DashboardPage.tsx?raw";
import liveOutputSource from "../pages/LiveOutputPage.tsx?raw";
import profileSource from "../pages/ProfileSelectionPage.tsx?raw";
import resultsSource from "../pages/ResultsPage.tsx?raw";
import testSelectionSource from "../pages/TestSelectionPage.tsx?raw";
import thresholdSource from "../pages/ThresholdConfigPage.tsx?raw";

// The page component that owns each nav destination. The expected label is the <h2>
// string read OUT OF THAT SOURCE at test time -- not a constant repeated here.
//
// That distinction is the whole point. A hand-written expectation would only pin the
// nav side: editing a page's <h2> would leave this file green while the two surfaces
// drifted, which is exactly the bug being fixed. Reading the heading from the source
// makes either side moving a failure. Proven by sabotage: changing the <h2> alone
// turns this red.
const PAGE_SOURCE: Record<PageId, string> = {
  dashboard: dashboardSource,
  cameras: cameraSource,
  profiles: profileSource,
  tests: testSelectionSource,
  config: thresholdSource,
  output: liveOutputSource,
  results: resultsSource
};

const ALL_PAGES = Object.keys(PAGE_SOURCE) as PageId[];

/** The first <h2>…</h2> literal in a page source: the heading the page shows. */
function headingOf(page: PageId): string {
  const match = /<h2>([^<{]+)<\/h2>/.exec(PAGE_SOURCE[page]);
  if (!match) throw new Error(`no literal <h2> heading found for page ${page}`);
  return match[1].trim();
}

const PAGE_HEADING = Object.fromEntries(ALL_PAGES.map((p) => [p, headingOf(p)])) as Record<PageId, string>;

function renderSidebar() {
  render(
    <Sidebar
      activePage="dashboard"
      onNavigate={vi.fn()}
      navigationAvailability={new Map(ALL_PAGES.map((p) => [p, true]))}
      isRunning={false}
      runStatus="Idle"
      actionInProgress={false}
      canStartDiagnostic
      onRequestStart={vi.fn()}
      onRequestStop={vi.fn()}
      theme="dark"
      onToggleTheme={vi.fn()}
    />
  );
}

describe("nav labels match the page they open", () => {
  it("renders every page's heading text as its nav entry", () => {
    renderSidebar();

    // Assertion observes: the accessible name of each nav BUTTON, compared against the
    // <h2> string the corresponding page renders. Not a substring test -- "Cameras"
    // is a substring of "Camera Selection" only in the reverse direction, and a
    // substring check would have passed on the very drift this locks out.
    const labels = [...document.querySelectorAll(".nav-group button, nav button")]
      .map((b) => b.textContent!.trim())
      .filter(Boolean);

    for (const page of ALL_PAGES) {
      expect(labels).toContain(PAGE_HEADING[page]);
    }
  });

  it("keeps no pre-rename label anywhere in the navigation", () => {
    renderSidebar();

    // Rule 7 (old-behaviour sweep): the rename is only done if the OLD names are gone.
    // Exact-match on the button labels, since "Cameras" must not survive as an entry
    // even though "Camera Selection" now exists.
    const labels = [...document.querySelectorAll("button")].map((b) => b.textContent!.trim());
    expect(labels).not.toContain("Cameras");
    expect(labels).not.toContain("Profiles");
    expect(screen.queryByText("Trigger Routing")).not.toBeInTheDocument();
  });

  it("mirrors the same names in the mobile select", () => {
    renderSidebar();

    // The select is a second surface listing the same pages; it drifts as easily as
    // the sidebar did. Assertion observes: the option text, in order.
    const options = [...screen.getByLabelText("Navigate").querySelectorAll("option")].map((o) =>
      o.textContent!.trim()
    );
    // Order is the nav order, and each name is the page's own heading -- so this
    // drifts red if either the select or a page heading moves alone.
    expect(options).toEqual(ALL_PAGES.map((p) => PAGE_HEADING[p]));
  });
});
