import { RunSummary } from "./types";

/**
 * Pass Rate (plan 2.8).
 *
 * The single place this is computed. The Dashboard formats the result and nothing more:
 * it used to sum every run's counters and divide once, which let a run with four
 * cameras -- or simply more tests -- dominate the headline figure, counted runs that
 * never produced a verdict, and showed 0% when there was nothing to measure at all.
 */

/** Counters must be real, non-negative numbers; anything else is corrupt data. */
function validCount(value: number): boolean {
  return Number.isFinite(value) && value >= 0;
}

/** The verdicts that count toward a rate. Skips are excluded by definition. */
function evaluatedCount(run: RunSummary): number {
  return run.pass_count + run.warn_count + run.fail_count;
}

/**
 * Whether a run may contribute to the overall Pass Rate.
 *
 * Deliberately an allow-list on `status`: treating an unrecognised status as valid
 * would silently admit any state added later. There is no duration floor -- a run that
 * finished quickly still measured something.
 */
export function runQualifiesForPassRate(run: RunSummary): boolean {
  if (run.status !== "completed") {
    return false;
  }
  // Absent means "written before the field existed", which is not evidence of
  // truncation. Only an explicit `true` disqualifies.
  if (run.truncated === true) {
    return false;
  }
  const counts = [run.pass_count, run.warn_count, run.fail_count, run.skip_count];
  if (!counts.every(validCount)) {
    return false;
  }
  // Nothing was evaluated: an all-skip run, or a run that recorded no verdict at all.
  // Averaging it in would mean inventing a rate for a run that has none.
  return evaluatedCount(run) > 0;
}

/**
 * One run's rate, from its own totals across every camera and test.
 *
 * `null` when the run evaluated nothing -- distinct from 0, which means every verdict
 * was a failure.
 */
export function runPassRate(run: RunSummary): number | null {
  const evaluated = evaluatedCount(run);
  if (evaluated <= 0) {
    return null;
  }
  return run.pass_count / evaluated;
}

/**
 * The arithmetic mean of the valid runs' rates: one run, one vote.
 *
 * Returns `null` when no run qualifies. That is NOT 0: zero means "everything failed",
 * null means "there is nothing to measure", and the Dashboard renders it as N/A.
 */
export function overallPassRate(runs: RunSummary[]): number | null {
  const rates: number[] = [];
  for (const run of runs) {
    if (!runQualifiesForPassRate(run)) {
      continue;
    }
    const rate = runPassRate(run);
    if (rate !== null) {
      rates.push(rate);
    }
  }
  if (rates.length === 0) {
    return null;
  }
  return rates.reduce((total, rate) => total + rate, 0) / rates.length;
}

/**
 * The mean duration of the valid runs, in milliseconds, or `null` when none qualify.
 *
 * Shares the validity filter with the Pass Rate on purpose: the two headline figures
 * describe the same set of runs, so a run excluded from one must be excluded from the
 * other.
 */
export function overallAverageDurationMs(runs: RunSummary[]): number | null {
  const durations = runs
    .filter(runQualifiesForPassRate)
    .map((run) => run.duration_ms)
    .filter(validCount);
  if (durations.length === 0) {
    return null;
  }
  return durations.reduce((total, ms) => total + ms, 0) / durations.length;
}
