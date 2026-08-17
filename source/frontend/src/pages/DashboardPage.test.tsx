import { afterEach, describe, expect, it, vi } from "vitest";
import { render, screen, waitFor } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { DashboardPage } from "./DashboardPage";

// The Configure flow ends at Test Configuration: v5 removed the format choice, so
// there is no Report Formats step (plan 2.10).
//
// This is a decision-conformance guard (protocol P8) as much as a copy test -- the
// user-facing walkthrough told people to visit a page that no longer exists.

/** Serves the given partial run summaries, filled out with the required fields. */
function stubRunsWith(partials: Array<Record<string, unknown>>) {
  vi.stubGlobal(
    "fetch",
    vi.fn(async () =>
      new Response(
        JSON.stringify({
          runs: partials.map((p) => ({
            trigger_mode: "free-run",
            trigger_profile_id: "",
            role_bindings: [],
            camera_paths: ["/dev/video0"],
            started_at_utc: "2026-08-04T10:00:00Z",
            finished_at_utc: "2026-08-04T10:01:00Z",
            duration_ms: 60000,
            reports: [],
            ...p
          }))
        }),
        { status: 200, headers: { "Content-Type": "application/json" } }
      )
    )
  );
}

function stubRuns() {
  vi.stubGlobal(
    "fetch",
    vi.fn(async () =>
      new Response(JSON.stringify({ runs: [] }), {
        status: 200,
        headers: { "Content-Type": "application/json" }
      })
    )
  );
}

afterEach(() => {
  vi.unstubAllGlobals();
});

describe("DashboardPage guided-run copy", () => {
  it("does not mention Report Formats anywhere in the user-facing text", async () => {
    stubRuns();
    render(<DashboardPage onViewRun={vi.fn()} onStartNewDiagnostic={vi.fn()} isRunning={false} />);

    await waitFor(() => expect(document.body.textContent).not.toContain("Loading"));
    expect(document.body.textContent).not.toContain("Report Formats");
    expect(document.body.textContent).not.toMatch(/report formats/i);
  });

  it("names the four Configure steps that remain, in order", async () => {
    stubRuns();
    render(<DashboardPage onViewRun={vi.fn()} onStartNewDiagnostic={vi.fn()} isRunning={false} />);

    await waitFor(() => expect(document.body.textContent).not.toContain("Loading"));
    const text = document.body.textContent!;
    const steps = ["Camera Selection", "Trigger Selection", "Test Selection", "Test Configuration"];
    const positions = steps.map((step) => text.indexOf(step));
    for (const [i, position] of positions.entries()) {
      expect(position, `${steps[i]} is missing from the walkthrough`).toBeGreaterThan(-1);
    }
    // Still in flow order, so the copy matches the navigation.
    expect(positions).toEqual([...positions].sort((a, b) => a - b));
  });

  // The backend has always sent trigger_mode in the run summary; the frontend type was
  // missing it, so a free-run row could not be told apart from a triggered run whose
  // profile went unrecorded -- both showed a bare dash (plan 2.7).
  it("distinguishes a free-run row from a triggered run with no profile", async () => {
    vi.stubGlobal(
      "fetch",
      vi.fn(async () =>
        new Response(
          JSON.stringify({
            runs: [
              {
                id: "free", status: "completed", trigger_mode: "free-run", trigger_profile_id: "",
                role_bindings: [], camera_paths: ["/dev/video0"], started_at_utc: "2026-08-04T10:00:00Z",
                finished_at_utc: "2026-08-04T10:01:00Z", duration_ms: 60000,
                pass_count: 1, fail_count: 0, warn_count: 0, skip_count: 0, reports: []
              },
              {
                id: "armed", status: "completed", trigger_mode: "hardware", trigger_profile_id: "bench-rig",
                role_bindings: [], camera_paths: ["/dev/video1"], started_at_utc: "2026-08-04T11:00:00Z",
                finished_at_utc: "2026-08-04T11:01:00Z", duration_ms: 60000,
                pass_count: 1, fail_count: 0, warn_count: 0, skip_count: 0, reports: []
              }
            ]
          }),
          { status: 200, headers: { "Content-Type": "application/json" } }
        )
      )
    );
    render(<DashboardPage onViewRun={vi.fn()} onStartNewDiagnostic={vi.fn()} isRunning={false} />);

    await waitFor(() => expect(screen.getByText("/dev/video0")).toBeInTheDocument());
    expect(screen.getByText("Not required (free-run)")).toBeInTheDocument();
    expect(screen.getByText("bench-rig")).toBeInTheDocument();
  });

  it("survives a run summary whose collections are empty", async () => {
    // An empty `reports` array must be an array, not a missing key: the row used to
    // crash on `undefined.map(...)` when a run wrote no artifacts (plan 2.7).
    vi.stubGlobal(
      "fetch",
      vi.fn(async () =>
        new Response(
          JSON.stringify({
            runs: [
              {
                id: "bare", status: "error", trigger_mode: "free-run", trigger_profile_id: "",
                role_bindings: [], camera_paths: [], started_at_utc: "2026-08-04T10:00:00Z",
                finished_at_utc: "2026-08-04T10:00:01Z", duration_ms: 1000,
                pass_count: 0, fail_count: 0, warn_count: 0, skip_count: 0, reports: []
              }
            ]
          }),
          { status: 200, headers: { "Content-Type": "application/json" } }
        )
      )
    );
    render(<DashboardPage onViewRun={vi.fn()} onStartNewDiagnostic={vi.fn()} isRunning={false} />);

    await waitFor(() => expect(document.body.textContent).not.toContain("Loading"));
    // Rendered rather than blown up.
    expect(screen.getByText("Not required (free-run)")).toBeInTheDocument();
  });

  // The page must FORMAT the shared helper's answer, not compute its own (plan 2.8).
  // These go through the rendered tiles, so a page that quietly reintroduced a pooled
  // calculation would fail even though the helper's own unit tests still passed.
  it("shows N/A rather than 0% when no run qualifies", async () => {
    // A running run and an all-skip run: nothing to measure. 0% would read as
    // "everything failed".
    stubRunsWith([
      { id: "a", status: "running", pass_count: 5, fail_count: 0, warn_count: 0, skip_count: 0 },
      { id: "b", status: "completed", pass_count: 0, fail_count: 0, warn_count: 0, skip_count: 26 }
    ]);
    render(<DashboardPage onViewRun={vi.fn()} onStartNewDiagnostic={vi.fn()} isRunning={false} />);

    await waitFor(() => expect(document.body.textContent).not.toContain("Loading"));
    const tile = screen.getByText("Pass Rate").closest(".stat-tile");
    expect(tile!.textContent).toContain("N/A");
    expect(tile!.textContent).not.toContain("0.0%");
  });

  it("averages run rates instead of pooling counters", async () => {
    // Pooled this is 50/90 = 55.6%; averaged it is (0.5 + 1.0) / 2 = 75%.
    stubRunsWith([
      {
        id: "multi", status: "completed", camera_paths: ["/a", "/b", "/c", "/d"],
        pass_count: 40, fail_count: 40, warn_count: 0, skip_count: 0
      },
      { id: "single", status: "completed", pass_count: 10, fail_count: 0, warn_count: 0, skip_count: 0 }
    ]);
    render(<DashboardPage onViewRun={vi.fn()} onStartNewDiagnostic={vi.fn()} isRunning={false} />);

    await waitFor(() => expect(document.body.textContent).not.toContain("Loading"));
    const tile = screen.getByText("Pass Rate").closest(".stat-tile");
    expect(tile!.textContent).toContain("75.0%");
    expect(tile!.textContent).not.toContain("55.6%");
  });

  it("keeps a genuine 0% distinct from N/A", async () => {
    stubRunsWith([{ id: "bad", status: "completed", pass_count: 0, fail_count: 4, warn_count: 0, skip_count: 0 }]);
    render(<DashboardPage onViewRun={vi.fn()} onStartNewDiagnostic={vi.fn()} isRunning={false} />);

    await waitFor(() => expect(document.body.textContent).not.toContain("Loading"));
    const tile = screen.getByText("Pass Rate").closest(".stat-tile");
    expect(tile!.textContent).toContain("0.0%");
    expect(tile!.textContent).not.toContain("N/A");
  });

  it("opens a historical run, which is the route to Result Output", async () => {
    // Result Output is explicitly kept (plan 4.7); the dashboard is how a past run is
    // opened. Asserting only that the row rendered would not prove the route works,
    // so this clicks it and checks the callback actually fires with the run id.
    const onViewRun = vi.fn();
    vi.stubGlobal(
      "fetch",
      vi.fn(async () =>
        new Response(
          JSON.stringify({
            runs: [
              {
                id: "run-1",
                status: "completed",
                trigger_profile_id: "bench-rig",
                role_bindings: [],
                camera_paths: ["/dev/video0"],
                started_at_utc: "2026-08-04T10:00:00Z",
                finished_at_utc: "2026-08-04T10:01:00Z",
                duration_ms: 60000,
                pass_count: 1,
                fail_count: 0,
                warn_count: 0,
                skip_count: 0,
                reports: []
              }
            ]
          }),
          { status: 200, headers: { "Content-Type": "application/json" } }
        )
      )
    );
    const user = userEvent.setup();
    render(<DashboardPage onViewRun={onViewRun} onStartNewDiagnostic={vi.fn()} isRunning={false} />);

    await waitFor(() => expect(screen.getByText("/dev/video0")).toBeInTheDocument());
    await user.click(screen.getByText("/dev/video0"));

    expect(onViewRun).toHaveBeenCalledWith("run-1");
    expect(onViewRun).toHaveBeenCalledTimes(1);
    expect(document.body.textContent).not.toContain("Report Formats");
  });
});
