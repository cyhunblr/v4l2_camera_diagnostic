import { Profile, RunSummary, StartRunPayload, TestDefinition, ThresholdConfig } from "./types";

export const DEFAULT_TESTS: TestDefinition[] = [
  {
    id: "t01-open-close",
    name: "Open / Close Cycle Test",
    category: "Basic Driver Reliability",
    description: "Tests basic v4l2 device open and close file handle operations.",
    uses_trigger: false,
    supported_trigger_modes: ["free-run", "software", "hardware"],
    tags: ["stable"]
  },
  {
    id: "t02-query-caps",
    name: "V4L2 Capability Query",
    category: "Basic Driver Reliability",
    description: "Queries V4L2 device capabilities and streaming flags.",
    uses_trigger: false,
    supported_trigger_modes: ["free-run", "software", "hardware"],
    tags: ["stable"]
  },
  {
    id: "t07-poll-timeout-sweep",
    name: "Poll Timeout Sweep",
    category: "Streaming & Latency",
    description: "Sweeps poll timeout durations to verify driver event notifications.",
    uses_trigger: true,
    supported_trigger_modes: ["free-run", "software", "hardware"],
    tags: ["long-running"]
  },
  {
    id: "t12-zero-copy-dmabuf",
    name: "Zero-Copy DMABUF Transfer",
    category: "Advanced Memory",
    description: "Verifies zero-copy memory pointer sharing via DMABUF handles.",
    uses_trigger: false,
    supported_trigger_modes: ["free-run", "software"],
    tags: ["device-specific", "stress"]
  }
];

export const DEFAULT_THRESHOLDS: ThresholdConfig[] = [
  {
    id: "default",
    name: "Built-in Standard Thresholds",
    description: "Default validation thresholds for Jetson diagnostic suite",
    values: {
      "t01-open-close": { "max_open_duration_ms": 150, "failure_rate_pct": 0 },

      "t07-poll-timeout-sweep": { "max_poll_delay_ms": 500, "drop_count": 0 }
    },
    params: {
      "t01-open-close": { "cycle_count": 10, "warmup_delay_ms": 20 },
      "t07-poll-timeout-sweep": { "sweep_steps": 5, "timeout_ms": 1000 }
    }
  },
  {
    id: "jetson-high-perf",
    name: "Jetson High Performance Profile",
    description: "Strict thresholds tailored for low-latency Tegra multimedia pipelines",
    values: {
      "t01-open-close": { "max_open_duration_ms": 50, "failure_rate_pct": 0 },
      "t07-poll-timeout-sweep": { "max_poll_delay_ms": 100, "drop_count": 0 }
    },
    params: {
      "t01-open-close": { "cycle_count": 50, "warmup_delay_ms": 10 },
      "t07-poll-timeout-sweep": { "sweep_steps": 10, "timeout_ms": 500 }
    }
  }
];

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


