import { Profile, RunSummary, StartRunPayload, ThresholdConfig } from "./types";

/** Thin fetch wrappers. Callers inspect `res.ok`/`res.status` themselves, matching
 *  the error-handling style the app already used before this file existed. */

export function getDevices() {
  return fetch("/api/devices");
}

export function getProfiles() {
  return fetch("/api/profiles");
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

export function getTests() {
  return fetch("/api/tests");
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

/** Historical run list for the Dashboard. The endpoint may not exist yet on the
 *  backend while it's being implemented in parallel — callers should catch and
 *  degrade gracefully (empty history), not treat this as fatal. */
export async function getRuns(): Promise<RunSummary[]> {
  const res = await fetch("/api/runs");
  if (!res.ok) {
    throw new Error(`Failed to fetch run history: ${res.status} ${res.statusText}`);
  }
  const json = await res.json();
  return Array.isArray(json.runs) ? json.runs : [];
}

// --- Threshold configuration API ---

export function getThresholds() {
  return fetch("/api/thresholds");
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
