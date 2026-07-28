import { HardDrive, CheckCircle2, Filter } from "lucide-react";
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

type ActionMode = "select-all" | "clear-all" | "reset-stable";

type Props = {
  groupedTests: Array<[string, TestDefinition[]]>;
  setSelectedTests: (tests: string[]) => void;
  tests: TestDefinition[];
  isTestSelected: (test: TestDefinition) => boolean;
  onToggleTest: (testId: string) => void;
  triggerMode: TriggerMode;
  backends: string[];
  onToggleBackend: (backend: string) => void;
  activeTags: string[];
  setActiveTags: (tags: string[]) => void;
  activeAction: ActionMode;
  setActiveAction: (action: ActionMode) => void;
};

export function TestSelectionPage({
  groupedTests,
  setSelectedTests,
  tests,
  isTestSelected,
  onToggleTest,
  triggerMode,
  backends,
  onToggleBackend,
  activeTags,
  setActiveTags,
  activeAction,
  setActiveAction
}: Props) {

  const allTests = tests.length ? tests : groupedTests.flatMap(([, items]) => items);
  const selectedCount = allTests.filter((t) => isTestSelected(t)).length;

  // Tests that are supported in current trigger + backend mode
  function isSupportedTest(t: TestDefinition) {
    const isTrig = t.supported_trigger_modes?.includes(triggerMode);
    const isBack = !t.requires_dmabuf || backends.includes("dmabuf");
    return isTrig && isBack;
  }

  // When a tag pill is clicked: toggle the tag, then auto-select tests matching any active tag
  function handleTagClick(tagId: string) {
    const newActiveTags = activeTags.includes(tagId)
      ? activeTags.filter((t) => t !== tagId)
      : [...activeTags, tagId];

    setActiveTags(newActiveTags);

    // Auto-select tests that have at least one active tag
    if (newActiveTags.length > 0) {
      const matchingIds = allTests
        .filter((t) => isSupportedTest(t) && t.tags?.some((tag) => newActiveTags.includes(tag)))
        .map((t) => t.id);
      setSelectedTests(matchingIds);
      setActiveAction("reset-stable"); // reset to neutral when tags change manually
    } else {
      // No tags active → clear all
      setSelectedTests([]);
    }
  }

  function handleSelectAll() {
    // All tags active, all supported tests selected
    setActiveTags(TAG_DEFINITIONS.map((td) => td.id));
    setSelectedTests(allTests.filter(isSupportedTest).map((t) => t.id));
    setActiveAction("select-all");
  }

  function handleClearAll() {
    // No tags active, no tests selected
    setActiveTags([]);
    setSelectedTests([]);
    setActiveAction("clear-all");
  }

  function handleResetStable() {
    // Only stable tag active, only stable-tagged supported tests selected
    setActiveTags(["stable"]);
    const stableIds = allTests
      .filter((t) => isSupportedTest(t) && t.tags?.includes("stable"))
      .map((t) => t.id);
    setSelectedTests(stableIds);
    setActiveAction("reset-stable");
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
          {/* Backend selector */}
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

          {/* Tag filters — clicking auto-selects matching tests */}
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
                  onClick={() => handleTagClick(def.id)}
                >
                  <span>{def.label}</span>
                </button>
              ))}
            </div>
          </div>

          <div className="toolbar-divider" />

          {/* Action cards — radio-style, only one active at a time */}
          <div className="filter-group actions-group">
            <div className="action-card-group">
              <button
                type="button"
                className={`action-card ${activeAction === "select-all" ? "action-card-active" : ""}`}
                onClick={handleSelectAll}
              >
                Select All
              </button>
              <button
                type="button"
                className={`action-card ${activeAction === "clear-all" ? "action-card-active" : ""}`}
                onClick={handleClearAll}
              >
                Clear All
              </button>
              <button
                type="button"
                className={`action-card ${activeAction === "reset-stable" ? "action-card-active" : ""}`}
                onClick={handleResetStable}
              >
                Reset Default (Stable)
              </button>
            </div>
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

                  // A test is "dimmed" (deactivated) if it has no active tag match AND is not selected
                  const hasActiveTag =
                    activeTags.length === 0
                      ? false
                      : test.tags?.some((tag) => activeTags.includes(tag));
                  const isSelected = isTestSelected(test);
                  const isDimmed = !isSelected && !hasActiveTag;

                  let subtitleText = test.name;
                  if (!isTriggerSupported) {
                    subtitleText = `${test.name} · unavailable in ${triggerMode} mode`;
                  } else if (!isBackendSupported) {
                    subtitleText = `${test.name} · requires dmabuf backend`;
                  }

                  return (
                    <SelectableCard
                      key={test.id}
                      selected={isSelected}
                      onToggle={() => isSupported && onToggleTest(test.id)}
                      disabled={!isSupported || isDimmed}
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
