import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { render, screen, waitFor } from "@testing-library/react";
import { ResultsPage } from "./ResultsPage";
import { clearHistoricalResultsCache } from "./historicalResultsCache";

// The backend distinguishes "this run produced no tests" from "this run's structured
// result is unavailable" (plan 2.6.1: `structured_result_unavailable` plus a reason).
// Rendering both as an empty table hid the reason from the user, so the two states are
// kept apart here -- and a failure is never cached as a successful empty result.

const REPORTS = {
  reports: [
    { format: "html", url: "/reports/run-1/report.html" },
    { format: "json", url: "/reports/run-1/report.json" }
  ]
};

const RESULT = {
  id: "run-1",
  status: "completed",
  result: {
    result_schema_version: 1,
    project_name: "v4l2-camera-diagnostic",
    cameras: [
      {
        camera_path: "/dev/video0",
        role: "master",
        trigger_description: "",
        memory_backends: ["mmap"],
        tests: [
          {
            id: "t01-device-compliance",
            name: "V4L2 Device Compliance",
            category: "discovery",
            memory_backend: "mmap",
            status: "pass",
            summary: "All good.",
            duration_ms: 10,
            metrics: [],
            details: [],
            notes: [],
            warnings: []
          }
        ]
      }
    ]
  }
};

/**
 * Answers /api/runs/{id} with `runResponse`. `reports` controls the artifact-link
 * request: "ok" serves the list, "fail" answers 500, "reject" makes the fetch itself
 * reject (a network error).
 */
function stubFetch(
  runResponse: { status: number; body: unknown },
  reports: "ok" | "fail" | "reject" = "ok",
  calls: string[] = []
) {
  const mock = vi.fn(async (url: RequestInfo | URL) => {
    const text = String(url);
    calls.push(text);
    if (text.includes("/reports")) {
      if (reports === "reject") {
        throw new TypeError("network error");
      }
      if (reports === "fail") {
        return new Response("nope", { status: 500 });
      }
      return new Response(JSON.stringify(REPORTS), {
        status: 200,
        headers: { "Content-Type": "application/json" }
      });
    }
    return new Response(JSON.stringify(runResponse.body), {
      status: runResponse.status,
      headers: { "Content-Type": "application/json" }
    });
  });
  vi.stubGlobal("fetch", mock);
  return mock;
}

// The cache is module-global, so without this a scenario that fetched successfully
// would make a later one render the cached copy without fetching -- making the outcome
// depend on test order. Every test below also uses its own run id.
beforeEach(() => {
  clearHistoricalResultsCache();
});

afterEach(() => {
  vi.unstubAllGlobals();
  clearHistoricalResultsCache();
});

describe("ResultsPage historical run", () => {
  it("renders the structured result when it is available", async () => {
    stubFetch({ status: 200, body: RESULT });
    render(<ResultsPage viewedRunId="run-available" liveSummaries={[]} liveReportLinks={[]} />);

    await waitFor(() => expect(screen.getByText("V4L2 Device Compliance")).toBeInTheDocument());
    expect(screen.queryByText(/Structured results are unavailable/)).not.toBeInTheDocument();
  });

  it("shows the backend's reason instead of an empty table", async () => {
    stubFetch({
      status: 404,
      body: {
        id: "run-1",
        status: "completed",
        error: "structured_result_unavailable",
        reason: "the run's JSON report artifact could not be read"
      }
    });
    render(<ResultsPage viewedRunId="run-unreadable" liveSummaries={[]} liveReportLinks={[]} />);

    await waitFor(() => expect(screen.getByText(/Structured results are unavailable/)).toBeInTheDocument());
    // The backend's own explanation reaches the user.
    expect(screen.getByText("the run's JSON report artifact could not be read")).toBeInTheDocument();
    // And it is not dressed up as a run with zero tests.
    expect(screen.queryByText(/No test results/i)).not.toBeInTheDocument();
  });

  it("still offers the artifact links when the structured result is unavailable", async () => {
    // The links come from their own endpoint, so a downloadable HTML report stays
    // usable even when the structured result cannot be read.
    stubFetch({
      status: 404,
      body: { error: "structured_result_unavailable", reason: "artifact missing" }
    });
    render(<ResultsPage viewedRunId="run-links" liveSummaries={[]} liveReportLinks={[]} />);

    await waitFor(() => expect(screen.getByText(/Structured results are unavailable/)).toBeInTheDocument());
    expect(screen.getByRole("link", { name: "HTML" })).toHaveAttribute("href", "/reports/run-1/report.html");
    expect(screen.getByRole("link", { name: "JSON" })).toHaveAttribute("href", "/reports/run-1/report.json");
  });

  it("does not cache a failure as a successful empty result", async () => {
    // A transient failure -- a report root not mounted yet, a half-written artifact --
    // must not freeze into a permanent "0 results" for the rest of the session.
    const calls: string[] = [];
    stubFetch({ status: 404, body: { error: "structured_result_unavailable", reason: "artifact missing" } }, "ok", calls);
    const first = render(<ResultsPage viewedRunId="run-2" liveSummaries={[]} liveReportLinks={[]} />);
    await waitFor(() => expect(screen.getByText(/Structured results are unavailable/)).toBeInTheDocument());
    first.unmount();
    vi.unstubAllGlobals();

    // Second visit, same run id, and this time the artifact is readable.
    const retryCalls: string[] = [];
    stubFetch({ status: 200, body: { ...RESULT, id: "run-2" } }, "ok", retryCalls);
    render(<ResultsPage viewedRunId="run-2" liveSummaries={[]} liveReportLinks={[]} />);

    // It refetched rather than serving a cached failure.
    await waitFor(() => expect(retryCalls.some((c) => c.includes("/api/runs/run-2"))).toBe(true));
    await waitFor(() => expect(screen.getByText("V4L2 Device Compliance")).toBeInTheDocument());
    expect(screen.queryByText(/Structured results are unavailable/)).not.toBeInTheDocument();
  });

  // The two requests are independent: one failing must not cost us the other.
  it("shows the results when only the report-link request fails", async () => {
    stubFetch({ status: 200, body: { ...RESULT, id: "run-nolinks" } }, "fail");
    render(<ResultsPage viewedRunId="run-nolinks" liveSummaries={[]} liveReportLinks={[]} />);

    await waitFor(() => expect(screen.getByText("V4L2 Device Compliance")).toBeInTheDocument());
    expect(screen.queryByText(/Structured results are unavailable/)).not.toBeInTheDocument();
    // No links, but the results are intact -- Promise.all used to reject the pair and
    // throw the structured result away.
    expect(screen.queryByRole("link", { name: "HTML" })).not.toBeInTheDocument();
  });

  it("keeps the backend reason when the report-link request also fails", async () => {
    stubFetch(
      {
        status: 404,
        body: { error: "structured_result_unavailable", reason: "the run's index entry names no JSON artifact" }
      },
      "reject"
    );
    render(<ResultsPage viewedRunId="run-both-fail" liveSummaries={[]} liveReportLinks={[]} />);

    await waitFor(() => expect(screen.getByText(/Structured results are unavailable/)).toBeInTheDocument());
    // The reason survives a rejected /reports fetch: it came from the run request.
    expect(screen.getByText("the run's index entry names no JSON artifact")).toBeInTheDocument();
    expect(screen.queryByRole("link", { name: "HTML" })).not.toBeInTheDocument();
  });

  it("caches a run that genuinely produced no tests", async () => {
    // An empty result is still an AVAILABLE result, so it caches like any other -- only
    // failures are excluded.
    const calls: string[] = [];
    stubFetch(
      { status: 200, body: { id: "run-empty", status: "completed", result: { result_schema_version: 1, cameras: [] } } },
      "ok",
      calls
    );
    const first = render(<ResultsPage viewedRunId="run-empty" liveSummaries={[]} liveReportLinks={[]} />);
    await waitFor(() => expect(calls.some((c) => c.includes("/api/runs/run-empty"))).toBe(true));
    await waitFor(() => expect(screen.queryByText(/Structured results are unavailable/)).not.toBeInTheDocument());
    const afterFirst = calls.filter((c) => c.includes("/api/runs/run-empty")).length;
    first.unmount();

    render(<ResultsPage viewedRunId="run-empty" liveSummaries={[]} liveReportLinks={[]} />);
    await waitFor(() => expect(screen.queryByText("Loading run results...")).not.toBeInTheDocument());
    expect(calls.filter((c) => c.includes("/api/runs/run-empty")).length).toBe(afterFirst);
  });

  it("caches a successful result so revisiting does not refetch", async () => {
    // The cache still exists -- only failures are excluded from it.
    const calls: string[] = [];
    stubFetch({ status: 200, body: { ...RESULT, id: "run-3" } }, "ok", calls);
    const first = render(<ResultsPage viewedRunId="run-3" liveSummaries={[]} liveReportLinks={[]} />);
    await waitFor(() => expect(screen.getByText("V4L2 Device Compliance")).toBeInTheDocument());
    const afterFirst = calls.filter((c) => c.includes("/api/runs/run-3")).length;
    first.unmount();

    render(<ResultsPage viewedRunId="run-3" liveSummaries={[]} liveReportLinks={[]} />);
    await waitFor(() => expect(screen.getByText("V4L2 Device Compliance")).toBeInTheDocument());
    expect(calls.filter((c) => c.includes("/api/runs/run-3")).length).toBe(afterFirst);
  });
});
