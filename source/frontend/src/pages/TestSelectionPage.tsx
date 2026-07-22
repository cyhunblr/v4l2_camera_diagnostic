import { ListChecks } from "lucide-react";
import { TestDefinition, TriggerMode } from "../types";
import { SelectableCard } from "../components/SelectableCard";
import { InfoPopover } from "../components/InfoPopover";

const GROUP_SELECTORS = ["implemented", "stable", "all"];

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
  triggerMode
}: Props) {
  return (
    <div className="page">
      <header className="topbar">
        <div>
          <p className="eyebrow">Configure</p>
          <h2>Test Selection</h2>
        </div>
      </header>

      <div className="panel">
        <div className="panel-title">
          <ListChecks size={18} />
          <h3>Tests</h3>
        </div>
        <div className="test-groups">
          <div className="choice-row compact">
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
          <div className="choice-row compact">
            <label className="inline-checkbox">
              <input type="checkbox" checked={includeLong} onChange={(e) => onIncludeLongChange(e.target.checked)} />
              include long-running
            </label>
            <label className="inline-checkbox">
              <input
                type="checkbox"
                checked={includeExperimental}
                onChange={(e) => onIncludeExperimentalChange(e.target.checked)}
              />
              include experimental
            </label>
          </div>

          {groupedTests.map(([category, items]) => (
            <details key={category}>
              <summary>{category} <span>{items.length}</span></summary>
              <div className="card-grid horizontal-grid">
                {items.map((test) => (
                  <SelectableCard
                    key={test.id}
                    selected={isTestSelected(test)}
                    onToggle={() => onToggleTest(test.id)}
                    disabled={!test.supported_trigger_modes?.includes(triggerMode)}
                    title={test.id}
                    subtitle={!test.supported_trigger_modes?.includes(triggerMode)
                      ? `${test.name} · unavailable in ${triggerMode}`
                      : test.name}
                    layout="horizontal"
                    cornerAction={
                      <InfoPopover
                        content={
                          <div>
                            <p>{test.description}</p>
                            <p className="info-popover-meta">
                              {test.implemented_in_core ? "implemented" : "not implemented"} ·{" "}
                              {test.long_running ? "long-running" : "quick"} ·{" "}
                              {test.risky ? "experimental" : "stable"}
                            </p>
                          </div>
                        }
                      />
                    }
                  />
                ))}
              </div>
            </details>
          ))}
        </div>
      </div>
    </div>
  );
}
