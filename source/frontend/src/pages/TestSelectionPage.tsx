import { ListChecks, HardDrive, Sliders, CheckCircle2 } from "lucide-react";
import { TestDefinition, TriggerMode } from "../types";
import { SelectableCard } from "../components/SelectableCard";
import { InfoPopover } from "../components/InfoPopover";

const GROUP_SELECTORS = ["implemented", "stable", "all"];
const BACKEND_OPTIONS = ["mmap", "dmabuf", "userptr"];

type Props = {
  groupedTests: Array<[string, TestDefinition[]]>;
  selectedTests: string[];
  isTestSelected: (test: TestDefinition) => boolean;
  onSetGroupSelector: (selector: string) => void;
  onToggleTest: (testId: string) => void;
  includeLong: boolean;
  onIncludeLongChange: (value: boolean) => void;
  includeExperimental: boolean;
  onIncludeExperimentalChange: (value: boolean) => void;
  triggerMode: TriggerMode;
  backends: string[];
  onToggleBackend: (backend: string) => void;
};

export function TestSelectionPage({
  groupedTests,
  selectedTests,
  isTestSelected,
  onSetGroupSelector,
  onToggleTest,
  includeLong,
  onIncludeLongChange,
  includeExperimental,
  onIncludeExperimentalChange,
  triggerMode,
  backends,
  onToggleBackend
}: Props) {
  // Count total supported & selected tests
  const allTests = groupedTests.flatMap(([, items]) => items);
  const selectedCount = allTests.filter((t) => isTestSelected(t)).length;

  return (
    <div className="page test-selection-page">
      <header className="topbar">
        <div>
          <p className="eyebrow">Configure</p>
          <h2>Test Selection</h2>
        </div>
        <div className="test-summary-pill">
          <CheckCircle2 size={15} />
          <span><strong>{selectedCount}</strong> of <strong>{allTests.length}</strong> tests active</span>
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
              <ListChecks size={15} />
              Preset:
            </span>
            <div className="segmented-control">
              {GROUP_SELECTORS.map((selector) => (
                <button
                  key={selector}
                  className={selectedTests.includes(selector) ? "selected" : ""}
                  onClick={() => onSetGroupSelector(selector)}
                >
                  {selector}
                </button>
              ))}
            </div>
          </div>

          <div className="toolbar-divider" />

          <div className="filter-group">
            <span className="toolbar-label">
              <Sliders size={15} />
              Filter:
            </span>
            <div className="filter-toggles">
              <button
                type="button"
                className={`toggle-switch-pill ${includeLong ? "active" : ""}`}
                onClick={() => onIncludeLongChange(!includeLong)}
              >
                <span>Long-running</span>
                <span className="switch-track"><span className="switch-thumb" /></span>
              </button>

              <button
                type="button"
                className={`toggle-switch-pill ${includeExperimental ? "active" : ""}`}
                onClick={() => onIncludeExperimentalChange(!includeExperimental)}
              >
                <span>Experimental</span>
                <span className="switch-track"><span className="switch-thumb" /></span>
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
                  <span className="category-badge">{categorySelectedCount} / {items.length} selected</span>
                </summary>
                <div className="card-grid horizontal-grid">
                  {items.map((test) => {
                    const isSupported = test.supported_trigger_modes?.includes(triggerMode);
                    return (
                      <SelectableCard
                        key={test.id}
                        selected={isTestSelected(test)}
                        onToggle={() => onToggleTest(test.id)}
                        disabled={!isSupported}
                        title={test.id}
                        subtitle={!isSupported ? `${test.name} · unavailable in ${triggerMode}` : test.name}
                        badges={
                          <div className="test-card-badges">
                            {test.implemented_in_core && <span className="chip chip-core">Core</span>}
                            {test.experimental && <span className="chip chip-exp">Experimental</span>}
                            {test.long_running && <span className="chip chip-long">Long</span>}
                          </div>
                        }
                        layout="horizontal"
                        cornerAction={
                          <InfoPopover
                            content={
                              <div>
                                <p>{test.description}</p>
                                <p className="info-popover-meta">
                                  {test.implemented_in_core ? "implemented" : "not implemented"} ·{" "}
                                  {test.long_running ? "long-running" : "quick"} ·{" "}
                                  {test.experimental ? "experimental" : test.risky ? "risky" : "stable"}
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

