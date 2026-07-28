import { useState } from "react";
import { HardDrive, CheckCircle2, CheckSquare, Square, RotateCcw, Filter } from "lucide-react";
import { TestDefinition, TriggerMode } from "../types";
import { SelectableCard } from "../components/SelectableCard";
import { InfoPopover } from "../components/InfoPopover";

const BACKEND_OPTIONS = ["mmap", "dmabuf", "userptr"];

const TAG_DEFINITIONS: Array<{ id: string; label: string; class: string }> = [
  { id: "stable", label: "Stable", class: "chip-stable" },
  { id: "device-specific", label: "Device Specific", class: "chip-device" },
  { id: "stress", label: "Stress", class: "chip-stress" },
  { id: "long-running", label: "Long Running", class: "chip-long" },
  { id: "benchmark", label: "Benchmark", class: "chip-benchmark" }
];

type Props = {
  groupedTests: Array<[string, TestDefinition[]]>;
  _selectedTests?: string[];
  setSelectedTests: (tests: string[]) => void;
  tests: TestDefinition[];
  isTestSelected: (test: TestDefinition) => boolean;
  onToggleTest: (testId: string) => void;
  triggerMode: TriggerMode;
  backends: string[];
  onToggleBackend: (backend: string) => void;
};

export function TestSelectionPage({
  groupedTests,
  _selectedTests,
  setSelectedTests,
  tests,
  isTestSelected,
  onToggleTest,
  triggerMode,
  backends,
  onToggleBackend
}: Props) {
  const [activeTags, setActiveTags] = useState<string[]>(["stable", "device-specific"]);

  const allTests = tests.length ? tests : groupedTests.flatMap(([, items]) => items);
  const selectedCount = allTests.filter((t) => isTestSelected(t)).length;

  function toggleTagFilter(tagId: string) {
    setActiveTags((prev) =>
      prev.includes(tagId) ? prev.filter((t) => t !== tagId) : [...prev, tagId]
    );
  }

  function handleSelectMatching() {
    const matchingIds = allTests
      .filter((t) => {
        const isTrig = t.supported_trigger_modes?.includes(triggerMode);
        const isBack = !t.requires_dmabuf || backends.includes("dmabuf");
        if (!isTrig || !isBack) return false;
        return t.tags?.some((tag) => activeTags.includes(tag));
      })
      .map((t) => t.id);
    setSelectedTests(matchingIds);
  }

  function handleClearAll() {
    setSelectedTests([]);
  }

  function handleResetDefault() {
    const defaultIds = allTests
      .filter((t) => {
        const isTrig = t.supported_trigger_modes?.includes(triggerMode);
        const isBack = !t.requires_dmabuf || backends.includes("dmabuf");
        if (!isTrig || !isBack) return false;
        return t.tags?.includes("stable");
      })
      .map((t) => t.id);
    setSelectedTests(defaultIds);
  }

  return (
    <div className="page test-selection-page">
      <header className="topbar">
        <div>
          <p className="eyebrow">Configure</p>
          <h2>Test Selection</h2>
        </div>
        <div className="test-summary-pill">
          <CheckCircle2 size={15} />
          <span>
            <strong>{selectedCount}</strong> of <strong>{allTests.length}</strong> tests active
          </span>
        </div>
      </header>

      <div className="panel combined-toolbar-panel">
        <div className="filter-toolbar">
          <div className="filter-group">
            <span className="toolbar-label">
              <HardDrive size={15} />
              Backend:
            </span>
            <div className="choice-row compact">
              {BACKEND_OPTIONS.map((backend) => (
                <button
                  key={backend}
                  className={`choice-btn ${backends.includes(backend) ? "selected" : ""}`}
                  onClick={() => onToggleBackend(backend)}
                >
                  {backend}
                </button>
              ))}
            </div>
          </div>

          <div className="toolbar-divider" />

          <div className="filter-group">
            <span className="toolbar-label">
              <Filter size={15} />
              Tag Filters:
            </span>
            <div className="tag-filter-pills">
              {TAG_DEFINITIONS.map((def) => (
                <button
                  key={def.id}
                  type="button"
                  className={`tag-pill ${def.class} ${activeTags.includes(def.id) ? "active" : ""}`}
                  onClick={() => toggleTagFilter(def.id)}
                >
                  <span>{def.label}</span>
                </button>
              ))}
            </div>
          </div>

          <div className="toolbar-divider" />

          <div className="filter-group actions-group">
            <button type="button" className="action-btn primary-action" onClick={handleSelectMatching}>
              <CheckSquare size={14} /> Select Matching
            </button>
            <button type="button" className="action-btn secondary-action" onClick={handleClearAll}>
              <Square size={14} /> Clear All
            </button>
            <button type="button" className="action-btn secondary-action" onClick={handleResetDefault}>
              <RotateCcw size={14} /> Reset (Stable)
            </button>
          </div>
        </div>
      </div>

      <div className="test-groups">
        {groupedTests.map(([category, items]) => {
          const categorySelectedCount = items.filter((t) => isTestSelected(t)).length;

          return (
            <details key={category} open className="test-category-group">
              <summary className="category-summary">
                <span className="category-name">{category}</span>
                <span className="category-badge">
                  {categorySelectedCount} / {items.length} selected
                </span>
              </summary>
              <div className="card-grid horizontal-grid">
                {items.map((test) => {
                  const isTriggerSupported = test.supported_trigger_modes?.includes(triggerMode);
                  const isBackendSupported = !test.requires_dmabuf || backends.includes("dmabuf");
                  const isSupported = isTriggerSupported && isBackendSupported;

                  let subtitleText = test.name;
                  if (!isTriggerSupported) {
                    subtitleText = `${test.name} · unavailable in ${triggerMode} mode`;
                  } else if (!isBackendSupported) {
                    subtitleText = `${test.name} · requires dmabuf backend`;
                  }

                  return (
                    <SelectableCard
                      key={test.id}
                      selected={isTestSelected(test)}
                      onToggle={() => isSupported && onToggleTest(test.id)}
                      disabled={!isSupported}
                      title={test.id}
                      subtitle={subtitleText}
                      badges={
                        <div className="test-card-badges">
                          {test.tags?.map((tag) => {
                            const def = TAG_DEFINITIONS.find((td) => td.id === tag);
                            return (
                              <span key={tag} className={`chip ${def?.class ?? "chip-default"}`}>
                                {def?.label ?? tag}
                              </span>
                            );
                          })}
                        </div>
                      }
                      layout="horizontal"
                      cornerAction={
                        <InfoPopover
                          content={
                            <div>
                              <p>{test.description}</p>
                              <p className="info-popover-meta">
                                Tags: {test.tags?.join(", ") || "none"}
                              </p>
                            </div>
                          }
                        />
                      }
                    />
                  );
                })}
              </div>
            </details>
          );
        })}
      </div>
    </div>
  );
}
