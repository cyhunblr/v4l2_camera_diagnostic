import { ReportLink, TestSummary } from "../types";

/**
 * A fetched historical run. `state` is explicit rather than inferred from an empty
 * summaries list: the backend distinguishes "this run produced no tests" from "this
 * run's structured result is unavailable" (plan 2.6.1), and collapsing the two hid the
 * reason it gave behind an empty table.
 */
export type HistoricalResult = {
  state: "available" | "unavailable";
  summaries: TestSummary[];
  reportLinks: ReportLink[];
  /** The backend's explanation, shown to the user when state is "unavailable". */
  reason: string;
};

/**
 * Cache for successfully fetched historical runs.
 *
 * Only `available` results are stored -- including ones with no tests, which are a
 * legitimate outcome. Caching a FAILURE would freeze a transient error (a report root
 * not mounted yet, a half-written artifact) into a permanent "0 test results".
 *
 * Lives in its own module rather than in ResultsPage.tsx so that file exports only its
 * component: mixing the two breaks React fast refresh.
 */
const cache = new Map<string, HistoricalResult>();

export function getCachedHistoricalResult(runId: string): HistoricalResult | undefined {
  return cache.get(runId);
}

export function cacheHistoricalResult(runId: string, result: HistoricalResult): void {
  cache.set(runId, result);
}

/** Test-only: drops the cache so each scenario starts clean, independent of order. */
export function clearHistoricalResultsCache(): void {
  cache.clear();
}
