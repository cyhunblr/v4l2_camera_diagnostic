import { Profile, RunSummary, StartRunPayload, TestDefinition, ThresholdConfig } from "./types";

// The test list and the preset list come from the backend registries. There is no
// hard-coded mirror here on purpose: the previous one drifted out of date and
// served invented tests and an invented preset whose ids matched no real test, so
// a failed request looked like real configuration instead of a failure.
export const DEFAULT_TESTS: TestDefinition[] = [];

export const DEFAULT_THRESHOLDS: ThresholdConfig[] = [];

function jsonResp(data: unknown): Response {
  return new Response(JSON.stringify(data), {
    status: 200,
    headers: { "Content-Type": "application/json" }
  });
}

/** Thin fetch wrappers with offline fallbacks. */

export function getDevices() {
  return fetch("/api/devices").catch(() => jsonResp({ devices: [] }));
}

export function getProfiles() {
  return fetch("/api/profiles").catch(() => jsonResp({ profiles: [] }));
}

export function createProfile(profile: Profile) {
  return fetch("/api/profiles", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(profile)
  });
}

export function updateProfile(profile: Profile) {
  return fetch(`/api/profiles/${encodeURIComponent(profile.id)}`, {
    method: "PUT",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(profile)
  });
}

export function deleteProfile(profileId: string) {
  return fetch(`/api/profiles/${encodeURIComponent(profileId)}`, { method: "DELETE" });
}

export function exportProfile(id: string) {
  return fetch(`/api/profiles/${encodeURIComponent(id)}/export`);
}

export function importProfile(jsonText: string) {
  return fetch("/api/profiles/import", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: jsonText
  });
}

export function getControlDevices() {
  return fetch("/api/control-devices");
}

export function testSoftwareTrigger(payload: {
  camera_path: string;
  profile_id: string;
  trigger_channel_id: string;
}) {
  return fetch("/api/triggers/test", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ ...payload, confirmed: true })
  });
}

export async function getTests() {
  try {
    const res = await fetch("/api/tests");
    if (res.ok) return res;
  } catch {
    // fallback below
  }
  return jsonResp({ tests: DEFAULT_TESTS });
}

export function getRunLogs(runId: string, after: number) {
  return fetch(`/api/runs/${runId}/logs?after=${after}`);
}

export function getRunReports(runId: string) {
  return fetch(`/api/runs/${runId}/reports`);
}

export function startRun(payload: StartRunPayload) {
  return fetch("/api/runs", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(payload)
  });
}

export function stopRun(runId: string) {
  return fetch(`/api/runs/${runId}/stop`, { method: "POST" });
}

export async function getRuns(): Promise<RunSummary[]> {
  try {
    const res = await fetch("/api/runs");
    if (!res.ok) return [];
    const json = await res.json();
    return Array.isArray(json.runs) ? json.runs : [];
  } catch {
    return [];
  }
}

// --- Threshold configuration API ---

export async function getThresholds() {
  try {
    const res = await fetch("/api/thresholds");
    if (res.ok) return res;
  } catch {
    // fallback below
  }
  return jsonResp({ configs: DEFAULT_THRESHOLDS });
}

export function getThreshold(id: string) {
  return fetch(`/api/thresholds/${encodeURIComponent(id)}`);
}

export function saveThreshold(config: ThresholdConfig) {
  return fetch(`/api/thresholds/${encodeURIComponent(config.id)}`, {
    method: "PUT",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(config)
  });
}

export function createThreshold(config: ThresholdConfig) {
  return fetch("/api/thresholds", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(config)
  });
}

export function deleteThreshold(id: string) {
  return fetch(`/api/thresholds/${encodeURIComponent(id)}`, { method: "DELETE" });
}

export function exportThreshold(id: string) {
  return fetch(`/api/thresholds/${encodeURIComponent(id)}/export`);
}

export function importThreshold(jsonText: string) {
  return fetch("/api/thresholds/import", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: jsonText
  });
}


