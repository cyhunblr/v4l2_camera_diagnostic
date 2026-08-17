import { describe, expect, it } from "vitest";
import { RunSummary } from "./types";
import { overallPassRate, runPassRate, runQualifiesForPassRate } from "./passRate";

// Pass Rate is an average of RUN rates, not a ratio of pooled counts (plan 2.8).
//
// The Dashboard used to sum every run's counters and divide once, so a run with four
// cameras -- or simply more tests -- pulled the headline number around far harder than
// a single-camera run. It also counted runs that never produced a verdict, and showed
// 0% when there was nothing to measure at all, which reads as "everything failed".

function run(over: Partial<RunSummary> = {}): RunSummary {
  return {
    id: "run-1",
    status: "completed",
    trigger_mode: "free-run",
    trigger_profile_id: "",
    role_bindings: [],
    camera_paths: ["/dev/video0"],
    started_at_utc: "2026-08-04T10:00:00Z",
    finished_at_utc: "2026-08-04T10:01:00Z",
    duration_ms: 60000,
    pass_count: 0,
    fail_count: 0,
    warn_count: 0,
    skip_count: 0,
    reports: [],
    ...over
  };
}

describe("runQualifiesForPassRate", () => {
  it("accepts only a completed run", () => {
    expect(runQualifiesForPassRate(run({ status: "completed", pass_count: 1 }))).toBe(true);
    // An allow-list, not a deny-list: an unrecognised status must not slip into the
    // average just because nobody thought to exclude it.
    for (const status of ["queued", "running", "stopped", "error", "", "finished", "COMPLETED"]) {
      expect(runQualifiesForPassRate(run({ status, pass_count: 1 })), `status "${status}"`).toBe(false);
    }
  });

  it("excludes a run with no evaluated verdict", () => {
    // All-skip, and the degenerate all-zero case.
    expect(runQualifiesForPassRate(run({ skip_count: 26 }))).toBe(false);
    expect(runQualifiesForPassRate(run())).toBe(false);
    // One real verdict is enough to qualify.
    expect(runQualifiesForPassRate(run({ skip_count: 25, warn_count: 1 }))).toBe(true);
  });

  it("keeps a successful but very short run", () => {
    // No arbitrary duration floor: a run that finished quickly still measured something.
    expect(runQualifiesForPassRate(run({ pass_count: 2, duration_ms: 1 }))).toBe(true);
    expect(runQualifiesForPassRate(run({ pass_count: 2, duration_ms: 0 }))).toBe(true);
  });

  it("excludes a truncated run but tolerates the field being absent", () => {
    expect(runQualifiesForPassRate(run({ pass_count: 1, truncated: true }))).toBe(false);
    expect(runQualifiesForPassRate(run({ pass_count: 1, truncated: false }))).toBe(true);
    // Older history entries predate the field. Their absence is not evidence of
    // truncation, so it must not disqualify them on its own.
    const legacy = run({ pass_count: 1 });
    expect("truncated" in legacy).toBe(false);
    expect(runQualifiesForPassRate(legacy)).toBe(true);
  });

  it("rejects negative or non-finite counters as invalid data", () => {
    for (const over of [
      { pass_count: -1, warn_count: 2 },
      { warn_count: -3, pass_count: 1 },
      { fail_count: -1, pass_count: 1 },
      { skip_count: -1, pass_count: 1 },
      { pass_count: Number.NaN, warn_count: 1 },
      { pass_count: Number.POSITIVE_INFINITY }
    ] as Array<Partial<RunSummary>>) {
      expect(runQualifiesForPassRate(run(over)), JSON.stringify(over)).toBe(false);
    }
  });
});

describe("runPassRate", () => {
  it("divides pass by the evaluated verdicts only", () => {
    expect(runPassRate(run({ pass_count: 3, warn_count: 1 }))).toBeCloseTo(0.75);
    expect(runPassRate(run({ pass_count: 1, fail_count: 1 }))).toBeCloseTo(0.5);
  });

  it("leaves skips out of both the numerator and the denominator", () => {
    // 100 skips must not move the rate at all.
    expect(runPassRate(run({ pass_count: 3, warn_count: 1, skip_count: 100 }))).toBeCloseTo(0.75);
    expect(runPassRate(run({ pass_count: 2, skip_count: 24 }))).toBeCloseTo(1);
  });

  it("returns null for a run with nothing to measure", () => {
    expect(runPassRate(run({ skip_count: 26 }))).toBeNull();
    expect(runPassRate(run())).toBeNull();
  });
});

describe("overallPassRate", () => {
  it("gives every valid run one vote regardless of camera count", () => {
    // Four cameras, four times the tests, but still one run. Pooling the counters made
    // this one dominate the headline figure.
    const multi = run({ id: "multi", camera_paths: ["/a", "/b", "/c", "/d"], pass_count: 40, fail_count: 40 });
    const single = run({ id: "single", pass_count: 10, fail_count: 0 });
    // Pooled it would be 50/90 = 55.6%; averaged it is (0.5 + 1.0) / 2 = 75%.
    expect(overallPassRate([multi, single])).toBeCloseTo(0.75);
  });

  it("averages a many-test and a few-test run equally", () => {
    const many = run({ id: "many", pass_count: 90, fail_count: 10 });  // 0.90
    const few = run({ id: "few", pass_count: 1, fail_count: 1 });      // 0.50
    expect(overallPassRate([many, few])).toBeCloseTo(0.7);
  });

  it("ignores runs that do not qualify", () => {
    const good = run({ id: "good", pass_count: 1 });                            // 1.0
    const skipped = run({ id: "skipped", skip_count: 26 });                     // excluded
    const stopped = run({ id: "stopped", status: "stopped", pass_count: 0, fail_count: 5 });
    const errored = run({ id: "errored", status: "error", pass_count: 0, fail_count: 5 });
    const cut = run({ id: "cut", pass_count: 0, fail_count: 5, truncated: true });
    expect(overallPassRate([good, skipped, stopped, errored, cut])).toBeCloseTo(1);
  });

  it("returns null -- not 0 -- when no run qualifies", () => {
    // 0% means "everything failed"; null means "there is nothing to measure". The
    // Dashboard renders the second as N/A.
    expect(overallPassRate([])).toBeNull();
    expect(overallPassRate([run({ skip_count: 26 })])).toBeNull();
    expect(overallPassRate([run({ status: "running", pass_count: 5 })])).toBeNull();
  });

  it("keeps a genuine 0% distinct from no data", () => {
    expect(overallPassRate([run({ fail_count: 4 })])).toBe(0);
  });
});
