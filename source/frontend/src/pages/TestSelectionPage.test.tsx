import { describe, expect, it, vi } from "vitest";
import { render, screen } from "@testing-library/react";
import { TestSelectionPage } from "./TestSelectionPage";
import { TestDefinition, groupTestsByLayer } from "../types";

// SelectableCard renders its title/subtitle/meta/badges inside a native
// <button>, which may only contain phrasing content and no nested control.
// Callers own what they pass in, so the invariant is checked here — at the
// call site that actually fills those slots.
const FORBIDDEN_IN_BUTTON = "div,p,ul,ol,li,h1,h2,h3,h4,h5,h6,table,section,article,header,footer";
const INTERACTIVE = "button,a[href],input,select,textarea,[tabindex]";

const test01: TestDefinition = {
  id: "t01-device-compliance",
  name: "V4L2 Device Compliance",
  category: "discovery",
  description: "Queries capabilities, memory backend support, formats, and frame sizes.",
  uses_trigger: false,
  supported_trigger_modes: ["free-run", "hardware", "software"],
  tags: ["stable", "benchmark"],
  layer: 1,
  layer_name: "Discovery"
};

function renderPage() {
  render(
    <TestSelectionPage
      groupedTests={[["Discovery", [test01]]]}
      setSelectedTests={vi.fn()}
      tests={[test01]}
      isTestSelected={() => false}
      onToggleTest={vi.fn()}
      triggerMode="free-run"
      backends={["mmap"]}
      onToggleBackend={vi.fn()}
      activeTags={[]}
      setActiveTags={vi.fn()}
      activeAction="reset-stable"
      setActiveAction={vi.fn()}
    />
  );
}

// Consumer-side proof that the page renders the shared grouping helper's output:
// the headings and their order must be exactly what groupTestsByLayer produces,
// so the helper's unit tests in types.test.ts actually govern this screen.
describe("TestSelectionPage layer grouping", () => {
  const t01: TestDefinition = { ...test01 };
  const t07: TestDefinition = {
    ...test01,
    id: "t07-multi-buffer",
    name: "Multi-buffer Configurations",
    layer: 3,
    layer_name: "Buffer & memory"
  };
  const t23: TestDefinition = {
    ...test01,
    id: "t23-sustained-capture",
    name: "Sustained Capture Stability",
    layer: 7,
    layer_name: "Stability"
  };

  it("renders one heading per layer, ordered by layer number", () => {
    // Shuffled on purpose: order must come from the layer number.
    const shuffled = [t23, t07, t01];
    render(
      <TestSelectionPage
        groupedTests={groupTestsByLayer(shuffled)}
        setSelectedTests={vi.fn()}
        tests={shuffled}
        isTestSelected={() => false}
        onToggleTest={vi.fn()}
        triggerMode="free-run"
        backends={["mmap"]}
        onToggleBackend={vi.fn()}
        activeTags={[]}
        setActiveTags={vi.fn()}
        activeAction="reset-stable"
        setActiveAction={vi.fn()}
      />
    );

    const rendered = [...document.querySelectorAll(".category-name")].map((el) => el.textContent);
    expect(rendered).toEqual(groupTestsByLayer(shuffled).map(([heading]) => heading));
    expect(rendered).toEqual([
      "Layer 1 — Discovery",
      "Layer 3 — Buffer & memory",
      "Layer 7 — Stability"
    ]);
  });

  it("shows each test under its own layer heading", () => {
    const tests = [t01, t07, t23];
    render(
      <TestSelectionPage
        groupedTests={groupTestsByLayer(tests)}
        setSelectedTests={vi.fn()}
        tests={tests}
        isTestSelected={() => false}
        onToggleTest={vi.fn()}
        triggerMode="free-run"
        backends={["mmap"]}
        onToggleBackend={vi.fn()}
        activeTags={[]}
        setActiveTags={vi.fn()}
        activeAction="reset-stable"
        setActiveAction={vi.fn()}
      />
    );

    for (const group of document.querySelectorAll(".test-category-group")) {
      const heading = group.querySelector(".category-name")!.textContent!;
      const expected = { 1: "t01-device-compliance", 3: "t07-multi-buffer", 7: "t23-sustained-capture" }[
        Number(heading.match(/^Layer (\d)/)![1])
      ];
      expect(group.textContent).toContain(expected!);
    }
  });
});

describe("TestSelectionPage", () => {
  it("renders a selectable card per test", () => {
    renderPage();
    expect(screen.getByRole("button", { name: /t01-device-compliance/ })).toBeInTheDocument();
  });

  it("keeps the card button free of block-level descendants", () => {
    renderPage();
    const cards = document.body.querySelectorAll("button.selectable-card");
    expect(cards.length).toBeGreaterThan(0);
    for (const card of cards) {
      expect(card.querySelectorAll(FORBIDDEN_IN_BUTTON)).toHaveLength(0);
    }
  });

  it("keeps the card button free of nested interactive descendants", () => {
    renderPage();
    const cards = document.body.querySelectorAll("button.selectable-card");
    expect(cards.length).toBeGreaterThan(0);
    for (const card of cards) {
      expect(card.querySelectorAll(INTERACTIVE)).toHaveLength(0);
    }
  });

  it("renders the info trigger outside the selection button", () => {
    renderPage();
    const card = document.body.querySelector("button.selectable-card");
    const info = document.body.querySelector(".card-corner-action button");
    expect(info).not.toBeNull();
    expect(card?.contains(info!)).toBe(false);
  });

  // Scoped to the card: "Stable"/"Benchmark" also appear as tag filter pills.
  it("shows each tag as a chip inside the card", () => {
    renderPage();
    const card = document.body.querySelector("button.selectable-card");
    const chips = [...(card?.querySelectorAll(".test-card-badges .chip") ?? [])];
    expect(chips.map((c) => c.textContent)).toEqual(["Stable", "Benchmark"]);
  });

  it("keeps supported test cards enabled even when activeTags is empty (Faz 4.4)", () => {
    renderPage();
    const card = screen.getByRole("button", { name: /t01-device-compliance/ });
    expect(card).not.toHaveAttribute("aria-disabled", "true");
  });
});
