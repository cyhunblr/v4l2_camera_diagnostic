import { afterEach, describe, expect, it, vi } from "vitest";
import { render, screen, waitFor } from "@testing-library/react";
import { ThresholdConfigPage } from "./ThresholdConfigPage";
import { TestDefinition, UNKNOWN_TEST_HEADING, groupTestIdsByLayer } from "../types";

// Consumer-side proof that the page renders the shared grouping helper's output.
// The headings and their order must be exactly what groupTestIdsByLayer
// produces, so the helper's unit tests in types.test.ts govern this screen too.
//
// The page used to derive the layer from the test number in the id, which put
// stale preset keys and any test past t26 into the same catch-all bucket.

function def(layer: number, layerName: string, id: string): TestDefinition {
  return {
    id,
    name: id,
    category: "discovery",
    description: "",
    uses_trigger: false,
    supported_trigger_modes: ["free-run"],
    tags: ["stable"],
    layer,
    layer_name: layerName
  };
}

const registry: TestDefinition[] = [
  def(1, "Discovery", "t01-device-compliance"),
  def(3, "Buffer & memory", "t07-multi-buffer"),
  def(7, "Stability", "t23-sustained-capture")
];

/** A preset carrying one key per registry test plus one the registry lost. */
function preset(ids: string[]) {
  const values: Record<string, Record<string, number>> = {};
  for (const id of ids) {
    values[id] = { sample_count: 20 };
  }
  return { id: "default", name: "Default", description: "", values, params: {} };
}

function stubThresholds(ids: string[]) {
  vi.stubGlobal(
    "fetch",
    vi.fn(async () =>
      new Response(JSON.stringify({ configs: [preset(ids)] }), {
        status: 200,
        headers: { "Content-Type": "application/json" }
      })
    )
  );
}

function renderPage(tests: TestDefinition[]) {
  render(
    <ThresholdConfigPage
      selectedThresholdId="default"
      onSelectedChange={vi.fn()}
      selectedTests={[]}
      tests={tests}
      onDirtyChange={vi.fn()}
      onError={vi.fn()}
    />
  );
}

function headings() {
  return [...document.querySelectorAll(".category-name")].map((el) => el.textContent);
}

afterEach(() => {
  vi.unstubAllGlobals();
});

describe("ThresholdConfigPage layer grouping", () => {
  it("groups preset keys by the layer the registry reports, ordered by layer number", async () => {
    const ids = ["t23-sustained-capture", "t01-device-compliance", "t07-multi-buffer"];
    stubThresholds(ids);
    renderPage(registry);

    await waitFor(() => expect(headings().length).toBeGreaterThan(0));

    // The page's own sort of preset keys is what the helper receives, so compare
    // against the helper applied to the same registry.
    const expected = groupTestIdsByLayer([...ids].sort(), registry).map(([heading]) => heading);
    expect(headings()).toEqual(expected);
    expect(headings()).toEqual([
      "Layer 1 — Discovery",
      "Layer 3 — Buffer & memory",
      "Layer 7 — Stability"
    ]);
  });

  it("shows unregistered preset keys under Unknown test, after the real layers", async () => {
    stubThresholds(["t01-device-compliance", "t99-removed-test", "t23-sustained-capture"]);
    renderPage(registry);

    await waitFor(() => expect(headings()).toContain(UNKNOWN_TEST_HEADING));

    expect(headings()).toEqual([
      "Layer 1 — Discovery",
      "Layer 7 — Stability",
      UNKNOWN_TEST_HEADING
    ]);
    // The stale key is visible rather than silently dropped or mislabelled. The
    // page title-cases the id for display ("t99-removed-test" -> "t99 Removed
    // Test"), so match the rendered form.
    const unknown = [...document.querySelectorAll(".test-category-group")].find(
      (group) => group.querySelector(".category-name")!.textContent === UNKNOWN_TEST_HEADING
    );
    expect(unknown!.textContent).toContain("t99 Removed Test");
    expect(unknown!.querySelector(".category-badge")!.textContent).toBe("1 test card(s)");
    // And it is the only card there: the registered tests stayed in their layers.
    expect(unknown!.textContent).not.toContain("t01");
    expect(unknown!.textContent).not.toContain("t23");
  });

  it("does not invent a layer while the registry is still empty", async () => {
    stubThresholds(["t01-device-compliance", "t23-sustained-capture"]);
    renderPage([]);

    await waitFor(() => expect(headings().length).toBeGreaterThan(0));

    expect(headings()).toEqual([UNKNOWN_TEST_HEADING]);
    expect(screen.queryByText(/^Layer \d/)).not.toBeInTheDocument();
  });

  it("displays migration warning banner when preset needs migration (Faz 4.5)", async () => {
    vi.stubGlobal(
      "fetch",
      vi.fn(async (url: string) => {
        if (url.includes("/api/thresholds/custom")) {
          return new Response(
            JSON.stringify({ id: "custom", name: "Custom", values: {}, warning: "Preset requires migration." }),
            { status: 200, headers: { "Content-Type": "application/json" } }
          );
        }
        return new Response(
          JSON.stringify({ configs: [{ id: "custom", name: "Custom", values: {}, params: {} }] }),
          { status: 200, headers: { "Content-Type": "application/json" } }
        );
      })
    );

    render(
      <ThresholdConfigPage
        selectedThresholdId="custom"
        onSelectedChange={vi.fn()}
        selectedTests={[]}
        tests={registry}
        onDirtyChange={vi.fn()}
        onError={vi.fn()}
      />
    );

    await waitFor(() => {
      expect(screen.getByText("Preset requires migration.")).toBeInTheDocument();
      expect(screen.getByRole("button", { name: "Migrate Config" })).toBeInTheDocument();
    });
  });
});
