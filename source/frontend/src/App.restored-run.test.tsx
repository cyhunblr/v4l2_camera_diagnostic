import { afterEach, describe, expect, it, vi } from "vitest";
import { render, screen, waitFor } from "@testing-library/react";
import App from "./App";

// A restored run must not unlock Result Output.
//
// useRunPolling rehydrates the last run id from localStorage so logs survive a
// page refresh. That restore used to hand App a terminal runStatus on mount,
// which unlocked "results" before the user had done anything in this session:
// opening the UI fresh showed Result Output clickable next to Dashboard.
// Result Output belongs to a run finishing in this session, or to a history
// entry the user opens from Dashboard.
//
// What each assertion observes: the DOM `disabled` property of the sidebar nav
// <button> whose accessible name is "Result Output" / "Live Output".

const RUN_ID_KEY = "v4l2diag_run_id";
const RUN_STATUS_KEY = "v4l2diag_run_status";

/** Answers every endpoint App touches on mount, plus logs for the restored run. */
function stubServer(logLines: unknown[]) {
  vi.stubGlobal(
    "fetch",
    vi.fn(async (input: RequestInfo | URL) => {
      const url = String(input);
      const json = (body: unknown) =>
        new Response(JSON.stringify(body), {
          status: 200,
          headers: { "Content-Type": "application/json" }
        });
      if (url.includes("/logs")) {
        return json({ status: "completed", lines: logLines, next_offset: logLines.length });
      }
      if (url.includes("/api/devices")) return json({ devices: [] });
      if (url.includes("/api/profiles")) return json({ profiles: [], schema_version: 1 });
      if (url.includes("/api/tests")) return json({ tests: [] });
      if (url.includes("/api/runs")) return json({ runs: [], reports: [], result: null });
      return json({});
    })
  );
}

function navButton(name: string): HTMLButtonElement {
  return screen.getByRole("button", { name: new RegExp(name, "i") }) as HTMLButtonElement;
}

afterEach(() => {
  vi.unstubAllGlobals();
  localStorage.clear();
});

describe("App navigation after a restored run", () => {
  it("keeps Result Output locked when localStorage holds a completed run", async () => {
    localStorage.setItem(RUN_ID_KEY, "web-run-20260810-120000");
    localStorage.setItem(RUN_STATUS_KEY, "completed");
    stubServer([]);

    render(<App />);

    // Wait for the mount-time restore to settle before asserting.
    await waitFor(() => expect(navButton("Dashboard")).toBeEnabled());
    await waitFor(() => {
      expect(navButton("Result Output").disabled).toBe(true);
    });
  });

  it("keeps Result Output locked even when the restored run has logs", async () => {
    localStorage.setItem(RUN_ID_KEY, "web-run-20260810-120000");
    localStorage.setItem(RUN_STATUS_KEY, "completed");
    stubServer([{ text: "T01 PASS", severity: "info" }]);

    render(<App />);

    // Logs still restore, so Live Output unlocks -- that path is unchanged.
    await waitFor(() => expect(navButton("Live Output").disabled).toBe(false));
    expect(navButton("Result Output").disabled).toBe(true);
  });

  it("leaves Result Output locked on a clean first load", async () => {
    stubServer([]);

    render(<App />);

    await waitFor(() => expect(navButton("Dashboard")).toBeEnabled());
    expect(navButton("Result Output").disabled).toBe(true);
  });
});
