import { describe, expect, it } from "vitest";
import {
  MASTER_ROLE,
  RunResultPayload,
  summariesFromResult,
  TestDefinition,
  expectedRoles,
  slaveRole,
  UNKNOWN_TEST_HEADING,
  groupTestIdsByLayer,
  groupTestsByLayer,
  testLayerHeading
} from "./types";

// The layer heading must come from backend metadata, never from the test id.
// The function this replaced parsed the number out of the id and mapped it
// through hard-coded ranges, so anything past t26 landed in an "Other
// Diagnostics" bucket and a renumbering silently regrouped the whole page.

function def(over: Partial<TestDefinition> = {}): TestDefinition {
  return {
    id: "t01-device-compliance",
    name: "V4L2 Device Compliance",
    category: "discovery",
    description: "",
    uses_trigger: false,
    supported_trigger_modes: ["free-run"],
    tags: ["stable"],
    layer: 1,
    layer_name: "Discovery",
    ...over
  };
}

describe("testLayerHeading", () => {
  it("composes the heading from the layer number and name", () => {
    expect(testLayerHeading(def({ layer: 3, layer_name: "Buffer & memory" }))).toBe("Layer 3 — Buffer & memory");
  });

  it("renders every one of the seven layers", () => {
    const layers: Array<[number, string]> = [
      [1, "Discovery"],
      [2, "State-machine correctness"],
      [3, "Buffer & memory"],
      [4, "Polling / timeout"],
      [5, "Latency"],
      [6, "Integrity"],
      [7, "Stability"]
    ];
    for (const [layer, layer_name] of layers) {
      expect(testLayerHeading(def({ layer, layer_name }))).toBe(`Layer ${layer} — ${layer_name}`);
    }
  });

  // The id is not consulted at all: a test numbered past t26, or one whose
  // number contradicts its layer, still lands in the layer the backend reported.
  it("ignores the test id entirely", () => {
    expect(testLayerHeading(def({ id: "t42-brand-new-test", layer: 4, layer_name: "Polling / timeout" }))).toBe(
      "Layer 4 — Polling / timeout"
    );
    expect(testLayerHeading(def({ id: "t01-device-compliance", layer: 7, layer_name: "Stability" }))).toBe(
      "Layer 7 — Stability"
    );
    expect(testLayerHeading(def({ id: "not-a-test-id", layer: 2, layer_name: "State-machine correctness" }))).toBe(
      "Layer 2 — State-machine correctness"
    );
  });

  it("never falls back to a catch-all bucket", () => {
    for (const id of ["t27-future", "t99-future", "zzz"]) {
      expect(testLayerHeading(def({ id, layer: 5, layer_name: "Latency" }))).not.toContain("Other Diagnostics");
    }
  });
});

const LAYERS: Array<[number, string]> = [
  [1, "Discovery"],
  [2, "State-machine correctness"],
  [3, "Buffer & memory"],
  [4, "Polling / timeout"],
  [5, "Latency"],
  [6, "Integrity"],
  [7, "Stability"]
];

function testFor(layer: number, id: string): TestDefinition {
  const name = LAYERS.find(([n]) => n === layer)![1];
  return def({ id, layer, layer_name: name });
}

describe("groupTestsByLayer", () => {
  it("orders groups 1-7 however the tests arrive", () => {
    // Deliberately shuffled: the previous implementation returned Map insertion
    // order, so the page reordered itself with the API response.
    const shuffled = [
      testFor(7, "t23-a"),
      testFor(3, "t07-a"),
      testFor(1, "t01-a"),
      testFor(5, "t14-a"),
      testFor(2, "t03-a"),
      testFor(6, "t20-a"),
      testFor(4, "t13-a")
    ];
    expect(groupTestsByLayer(shuffled).map(([heading]) => heading)).toEqual(
      LAYERS.map(([n, name]) => `Layer ${n} — ${name}`)
    );
  });

  it("puts every test of one layer in that layer's group", () => {
    const groups = groupTestsByLayer([
      testFor(3, "t07-a"),
      testFor(1, "t01-a"),
      testFor(3, "t08-a"),
      testFor(3, "t09-a")
    ]);
    expect(groups).toHaveLength(2);
    expect(groups[0][0]).toBe("Layer 1 — Discovery");
    expect(groups[0][1].map((t) => t.id)).toEqual(["t01-a"]);
    expect(groups[1][0]).toBe("Layer 3 — Buffer & memory");
    // Order within a group follows the input, so the registry's order is kept.
    expect(groups[1][1].map((t) => t.id)).toEqual(["t07-a", "t08-a", "t09-a"]);
  });

  it("returns nothing for no tests", () => {
    expect(groupTestsByLayer([])).toEqual([]);
  });
});

describe("groupTestIdsByLayer", () => {
  const registry = [testFor(1, "t01-a"), testFor(4, "t13-a"), testFor(7, "t23-a")];

  it("groups preset keys by the layer the registry reports", () => {
    const groups = groupTestIdsByLayer(["t23-a", "t01-a", "t13-a"], registry);
    expect(groups).toEqual([
      ["Layer 1 — Discovery", ["t01-a"]],
      ["Layer 4 — Polling / timeout", ["t13-a"]],
      ["Layer 7 — Stability", ["t23-a"]]
    ]);
  });

  it("collects unregistered keys under Unknown test, after the real layers", () => {
    const groups = groupTestIdsByLayer(["t99-gone", "t23-a", "t04-renamed", "t01-a"], registry);
    expect(groups.map(([heading]) => heading)).toEqual([
      "Layer 1 — Discovery",
      "Layer 7 — Stability",
      UNKNOWN_TEST_HEADING
    ]);
    expect(groups[groups.length - 1][1]).toEqual(["t99-gone", "t04-renamed"]);
  });

  it("shows only the Unknown group when the registry has not loaded", () => {
    expect(groupTestIdsByLayer(["t01-a", "t13-a"], [])).toEqual([
      [UNKNOWN_TEST_HEADING, ["t01-a", "t13-a"]]
    ]);
  });
});

// --- Role-based trigger routing (plan 2.5.1) ------------------------------
//
// The canonical role set mirrors the backend's role_bindings.hpp. Roles are
// derived from the run topology, never free text: a camera moving to a different
// /dev/videoN keeps its role, which is the whole point of routing by role.
describe("canonical run roles", () => {
  it("names slaves 1-based, following the slaves[] order", () => {
    expect(MASTER_ROLE).toBe("master");
    expect(slaveRole(0)).toBe("slave-1");
    expect(slaveRole(1)).toBe("slave-2");
    expect(slaveRole(10)).toBe("slave-11");
  });

  it("derives the expected role set from the topology, in order", () => {
    expect(expectedRoles(0)).toEqual(["master"]);
    expect(expectedRoles(1)).toEqual(["master", "slave-1"]);
    expect(expectedRoles(3)).toEqual(["master", "slave-1", "slave-2", "slave-3"]);
  });

  it("stays in topology order past nine slaves, where lexicographic order differs", () => {
    // "slave-10" sorts before "slave-2", so anything that sorted these by name
    // would mis-order an eleven-camera run and route triggers to the wrong cameras.
    const roles = expectedRoles(11);
    expect(roles[2]).toBe("slave-2");
    expect(roles[10]).toBe("slave-10");
    expect(roles).toEqual([...roles].slice());
    expect(roles).not.toEqual([...roles].sort());
  });
});

// --- Structured results replace the log-text regex (plan 2.6.1) -----------
//
// Two byte-identical copies of `line.message.match(/^(.+?) \[(.+?)\] (.+)$/)` used to
// derive a test's status from a human-readable log line. When the wording did not
// match, both fell through to a literal "done" status -- a value the backend never
// emits. The status now comes from the structured field.
describe("summariesFromResult", () => {
  function payload(over: Partial<RunResultPayload> = {}): RunResultPayload {
    return {
      result_schema_version: 1,
      project_name: "v4l2-camera-diagnostic",
      started_at_utc: "",
      finished_at_utc: "",
      host_name: "",
      kernel_release: "",
      kernel_version: "",
      run_mode: "sequential",
      trigger_mode: "free-run",
      trigger_profile_id: "",
      role_bindings: [],
      cameras: [],
      ...over
    };
  }

  function camera(path: string, tests: Array<{ id: string; name?: string; status: string; summary: string }>) {
    return {
      camera_path: path,
      role: "master",
      trigger_description: "",
      memory_backends: ["mmap"],
      tests: tests.map((t) => ({
        id: t.id,
        name: t.name ?? "",
        category: "",
        memory_backend: "mmap",
        status: t.status,
        summary: t.summary,
        duration_ms: 0,
        metrics: [],
        details: [],
        notes: [],
        warnings: []
      }))
    };
  }

  it("takes the status from the backend field, not from a message", () => {
    const rows = summariesFromResult(
      payload({
        cameras: [
          camera("/dev/video0", [
            { id: "t01-device-compliance", name: "V4L2 Device Compliance", status: "pass", summary: "All good." },
            { id: "t14-trigger-latency", name: "Trigger Latency", status: "warn", summary: "Jitter is high." }
          ])
        ]
      })
    );
    expect(rows).toEqual([
      { test: "V4L2 Device Compliance", status: "pass", message: "All good.", camera: "/dev/video0" },
      { test: "Trigger Latency", status: "warn", message: "Jitter is high.", camera: "/dev/video0" }
    ]);
    // "done" was the old regex's fallback; it is not a backend status.
    expect(rows.map((r) => r.status)).not.toContain("done");
  });

  it("keeps a summary that would have broken the old regex", () => {
    // No " [status] " shape anywhere: the old parser produced status "done" and put the
    // whole line in the message. Structured data does not care about the wording.
    const rows = summariesFromResult(
      payload({ cameras: [camera("/dev/video0", [{ id: "t03", status: "fail", summary: "no brackets here" }])] })
    );
    expect(rows[0].status).toBe("fail");
    expect(rows[0].message).toBe("no brackets here");
  });

  it("labels each row with its own camera", () => {
    const rows = summariesFromResult(
      payload({
        cameras: [
          camera("/dev/video0", [{ id: "t01", status: "pass", summary: "a" }]),
          camera("/dev/video1", [{ id: "t01", status: "fail", summary: "b" }])
        ]
      })
    );
    expect(rows.map((r) => r.camera)).toEqual(["/dev/video0", "/dev/video1"]);
  });

  it("falls back to the test id when the name is empty", () => {
    const rows = summariesFromResult(
      payload({ cameras: [camera("/dev/video0", [{ id: "t26-cold-start", status: "skipped", summary: "" }])] })
    );
    expect(rows[0].test).toBe("t26-cold-start");
  });

  it("returns nothing for a missing or empty result", () => {
    expect(summariesFromResult(null)).toEqual([]);
    expect(summariesFromResult(undefined)).toEqual([]);
    expect(summariesFromResult(payload())).toEqual([]);
  });
});
