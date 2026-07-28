import { useCallback, useEffect, useRef, useState } from "react";
import { SlidersHorizontal, Download, Upload, Plus, Trash2, Save, RotateCcw, AlertTriangle, Layers } from "lucide-react";
import { ThresholdConfig, getTestLayerName } from "../types";
import * as api from "../api";

function formatTestName(testId: string): string {
  const parts = testId.split("-");
  const num = parts[0];
  const words = parts.slice(1).map((w) => w.charAt(0).toUpperCase() + w.slice(1));
  return `${num} ${words.join(" ")}`;
}

function unitForKey(key: string): string {
  if (key.endsWith("_ms")) return "ms";
  if (key.endsWith("_pct")) return "%";
  return "cnt";
}

function formatKey(key: string): string {
  return key
    .replace(/_ms$/, "")
    .replace(/_pct$/, "")
    .replace(/_/g, " ");
}

/** Determines if a key expects an integer value. */
function isIntegerKey(key: string): boolean {
  const unit = unitForKey(key);
  if (unit === "cnt") return true;
  if (key.includes("count") || key.includes("step") || key.includes("cycles") || key.includes("limit")) {
    return true;
  }
  return false;
}

type Props = {
  selectedThresholdId: string;
  onSelectedChange: (id: string) => void;
  selectedTests?: string[];
  onError: (msg: string) => void;
};

export function ThresholdConfigPage({
  selectedThresholdId,
  onSelectedChange,
  selectedTests = [],
  onError
}: Props) {
  const [configs, setConfigs] = useState<ThresholdConfig[]>(api.DEFAULT_THRESHOLDS);
  const [editing, setEditing] = useState<ThresholdConfig | null>(null);
  const [dirty, setDirty] = useState(false);
  const [saving, setSaving] = useState(false);
  const [newId, setNewId] = useState("");
  const [showNew, setShowNew] = useState(false);
  const [onlySelected, setOnlySelected] = useState(true);
  const [inputErrors, setInputErrors] = useState<Record<string, string>>({});
  const [rawInputs, setRawInputs] = useState<Record<string, string>>({});
  const fileInputRef = useRef<HTMLInputElement>(null);

  const defaultConfig = configs.find((c) => c.id === "default");

  const loadConfigs = useCallback(async () => {
    try {
      const res = await api.getThresholds();
      if (!res.ok) return;
      const json = await res.json();
      setConfigs(json.configs ?? []);
    } catch {
      onError("Failed to load threshold configurations.");
    }
  }, [onError]);

  useEffect(() => {
    loadConfigs();
  }, [loadConfigs]);

  useEffect(() => {
    const found = configs.find((c) => c.id === selectedThresholdId);
    if (found) {
      setEditing(structuredClone(found));
      setDirty(false);
      setInputErrors({});
      setRawInputs({});
    } else if (configs.length > 0) {
      onSelectedChange(configs[0].id);
    }
  }, [selectedThresholdId, configs, onSelectedChange]);

  /** Inline dynamic validator for input changes. */
  function validateAndUpdate(
    testId: string,
    key: string,
    rawVal: string,
    section: "values" | "params"
  ) {
    if (!editing) return;
    const inputKey = `${testId}__${section}__${key}`;

    setRawInputs((prev) => ({ ...prev, [inputKey]: rawVal }));

    const requiresInt = isIntegerKey(key);
    const num = Number(rawVal);

    let errorMsg = "";
    if (rawVal.trim() === "" || isNaN(num)) {
      errorMsg = "Must be a valid number";
    } else if (requiresInt && !Number.isInteger(num)) {
      errorMsg = "Must be a whole integer";
    } else if (num < 0 && (key.includes("ms") || key.includes("count") || key.includes("pct"))) {
      errorMsg = "Must be non-negative";
    }

    setInputErrors((prev) => {
      const next = { ...prev };
      if (errorMsg) {
        next[inputKey] = errorMsg;
      } else {
        delete next[inputKey];
      }
      return next;
    });

    if (!errorMsg) {
      const next = structuredClone(editing);
      if (section === "values") {
        if (!next.values[testId]) next.values[testId] = {};
        next.values[testId][key] = num;
      } else {
        if (!next.params) next.params = {};
        if (!next.params[testId]) next.params[testId] = {};
        next.params[testId][key] = num;
      }
      setEditing(next);
      setDirty(true);
    }
  }

  function handleResetTestCard(testId: string) {
    if (!editing || !defaultConfig) return;
    const next = structuredClone(editing);
    if (defaultConfig.values[testId]) {
      next.values[testId] = structuredClone(defaultConfig.values[testId]);
    }
    if (defaultConfig.params?.[testId]) {
      if (!next.params) next.params = {};
      next.params[testId] = structuredClone(defaultConfig.params[testId]);
    }
    setEditing(next);
    setDirty(true);

    setRawInputs((prev) => {
      const nextRaw = { ...prev };
      Object.keys(nextRaw).forEach((k) => {
        if (k.startsWith(`${testId}__`)) delete nextRaw[k];
      });
      return nextRaw;
    });
    setInputErrors((prev) => {
      const nextErr = { ...prev };
      Object.keys(nextErr).forEach((k) => {
        if (k.startsWith(`${testId}__`)) delete nextErr[k];
      });
      return nextErr;
    });
  }

  async function handleSave() {
    if (!editing || !dirty || Object.keys(inputErrors).length > 0) return;
    setSaving(true);
    try {
      const res = await api.saveThreshold(editing);
      if (!res.ok) {
        const json = await res.json();
        onError(json.error || "Failed to save threshold config.");
      } else {
        setDirty(false);
        await loadConfigs();
      }
    } catch {
      onError("Network error saving threshold config.");
    } finally {
      setSaving(false);
    }
  }

  async function handleCreate() {
    const id = newId.trim().toLowerCase().replace(/[^a-z0-9_-]/g, "-");
    if (!id) return;
    const config: ThresholdConfig = {
      id,
      name: id,
      description: "",
      values: defaultConfig ? structuredClone(defaultConfig.values) : {},
      params: defaultConfig?.params ? structuredClone(defaultConfig.params) : {}
    };
    try {
      const res = await api.createThreshold(config);
      if (!res.ok) {
        const json = await res.json();
        onError(json.error || "Failed to create threshold config.");
      } else {
        setShowNew(false);
        setNewId("");
        await loadConfigs();
        onSelectedChange(id);
      }
    } catch {
      onError("Network error creating threshold config.");
    }
  }

  async function handleDelete() {
    if (!editing || editing.id === "default") return;
    try {
      const res = await api.deleteThreshold(editing.id);
      if (!res.ok) {
        const json = await res.json();
        onError(json.error || "Cannot delete this config.");
      } else {
        onSelectedChange("default");
        await loadConfigs();
      }
    } catch {
      onError("Network error deleting config.");
    }
  }

  async function handleExport() {
    if (!editing) return;
    try {
      const res = await api.exportThreshold(editing.id);
      if (!res.ok) return;
      const text = await res.text();
      const blob = new Blob([text], { type: "application/json" });
      const url = URL.createObjectURL(blob);
      const a = document.createElement("a");
      a.href = url;
      a.download = `${editing.id}-thresholds.json`;
      a.click();
      URL.revokeObjectURL(url);
    } catch {
      onError("Failed to export config.");
    }
  }

  function handleImportClick() {
    fileInputRef.current?.click();
  }

  async function handleImportFile(e: React.ChangeEvent<HTMLInputElement>) {
    const file = e.target.files?.[0];
    if (!file) return;
    try {
      const text = await file.text();
      const res = await api.importThreshold(text);
      if (!res.ok) {
        const json = await res.json();
        onError(json.error || "Import failed.");
      } else {
        await loadConfigs();
      }
    } catch {
      onError("Failed to import config file.");
    }
    e.target.value = "";
  }

  const isDefault = editing?.id === "default";

  // Collect all test IDs in editing config
  const allTestIds = editing
    ? [...new Set([...Object.keys(editing.values), ...Object.keys(editing.params ?? {})])].sort()
    : [];

  // Filter test IDs if onlySelected is active
  const displayTestIds = allTestIds.filter((id) => {
    if (!onlySelected) return true;
    if (selectedTests.length === 0 || selectedTests.includes("all") || selectedTests.includes("stable")) return true;
    return selectedTests.includes(id);
  });

  // Group display test IDs by Layer
  const categoryMap = new Map<string, string[]>();
  for (const id of displayTestIds) {
    const layerName = getTestLayerName(id);
    if (!categoryMap.has(layerName)) categoryMap.set(layerName, []);
    categoryMap.get(layerName)!.push(id);
  }

  // Count modified parameters across preset
  let modifiedCount = 0;
  if (editing && defaultConfig) {
    for (const testId of allTestIds) {
      const defVals = defaultConfig.values[testId] ?? {};
      const defParams = defaultConfig.params?.[testId] ?? {};
      const curVals = editing.values[testId] ?? {};
      const curParams = editing.params?.[testId] ?? {};
      for (const [k, v] of Object.entries(curVals)) {
        if (defVals[k] !== undefined && defVals[k] !== v) modifiedCount++;
      }
      for (const [k, v] of Object.entries(curParams)) {
        if (defParams[k] !== undefined && defParams[k] !== v) modifiedCount++;
      }
    }
  }

  const hasErrors = Object.keys(inputErrors).length > 0;

  return (
    <div className="page test-config-page">
      <header className="topbar">
        <div>
          <p className="eyebrow">Configure</p>
          <h2>Test Configuration</h2>
        </div>
        {modifiedCount > 0 && (
          <div className="test-summary-pill modified-summary-pill">
            <SlidersHorizontal size={15} />
            <span>
              <strong>{modifiedCount}</strong> parameter(s) modified
            </span>
          </div>
        )}
      </header>

      <div className="panel config-toolbar-panel">
        <div className="panel-title">
          <SlidersHorizontal size={18} />
          <h3>Preset Configuration Manager</h3>
        </div>

        <div className="threshold-toolbar">
          <div className="toolbar-select-group">
            <label htmlFor="preset-select">Active Preset:</label>
            <select
              id="preset-select"
              value={selectedThresholdId}
              onChange={(e) => onSelectedChange(e.target.value)}
            >
              {configs.map((c) => (
                <option key={c.id} value={c.id}>
                  {c.name} {c.id === "default" ? "(Built-in Default)" : ""}
                </option>
              ))}
            </select>
          </div>

          <div className="toolbar-actions">
            <button className="icon-text-button" onClick={() => setShowNew(true)} title="New config">
              <Plus size={15} /> New Preset
            </button>
            <button className="icon-text-button" onClick={handleExport} title="Export" disabled={!editing}>
              <Download size={15} /> Export
            </button>
            <button className="icon-text-button" onClick={handleImportClick} title="Import">
              <Upload size={15} /> Import
            </button>
            {!isDefault && (
              <button className="icon-text-button danger" onClick={handleDelete} title="Delete">
                <Trash2 size={15} /> Delete
              </button>
            )}
          </div>
          <input
            ref={fileInputRef}
            type="file"
            accept=".json"
            style={{ display: "none" }}
            onChange={handleImportFile}
          />
        </div>

        {showNew && (
          <div className="threshold-new-row">
            <input
              type="text"
              placeholder="config-id (lowercase, no spaces)"
              value={newId}
              onChange={(e) => setNewId(e.target.value)}
              onKeyDown={(e) => e.key === "Enter" && handleCreate()}
            />
            <button className="primary-btn" onClick={handleCreate}>Create</button>
            <button className="secondary-btn" onClick={() => setShowNew(false)}>Cancel</button>
          </div>
        )}

        <div className="config-filter-bar">
          <div className="filter-group">
            <span className="toolbar-label">
              <Layers size={15} /> Scope:
            </span>
            <div className="choice-row compact">
              <button
                type="button"
                className={`choice-btn ${onlySelected ? "selected" : ""}`}
                onClick={() => setOnlySelected(true)}
              >
                Selected Tests ({displayTestIds.length})
              </button>
              <button
                type="button"
                className={`choice-btn ${!onlySelected ? "selected" : ""}`}
                onClick={() => setOnlySelected(false)}
              >
                All Tests ({allTestIds.length})
              </button>
            </div>
          </div>
        </div>

        {editing && (
          <div className="threshold-editor">
            {isDefault && (
              <p className="threshold-hint">
                The default preset is read-only. Create a custom preset to modify parameters.
              </p>
            )}

            {[...categoryMap.entries()].map(([category, catTestIds]) => (
              <details key={category} open className="test-category-group">
                <summary className="category-summary">
                  <span className="category-name">{category}</span>
                  <span className="category-badge">{catTestIds.length} test card(s)</span>
                </summary>

                <div className="card-grid horizontal-grid compact-grid">
                  {catTestIds.map((testId) => {
                    const defaultValues = defaultConfig?.values[testId] ?? {};
                    const defaultParams = defaultConfig?.params?.[testId] ?? {};
                    const testValues = editing.values[testId] ?? {};
                    const testParams = editing.params?.[testId] ?? {};
                    const hasParams = Object.keys(testParams).length > 0 || Object.keys(defaultParams).length > 0;
                    const hasValues = Object.keys(testValues).length > 0 || Object.keys(defaultValues).length > 0;

                    let cardModified = false;
                    for (const [k, v] of Object.entries(testValues)) {
                      if (defaultValues[k] !== undefined && defaultValues[k] !== v) cardModified = true;
                    }
                    for (const [k, v] of Object.entries(testParams)) {
                      if (defaultParams[k] !== undefined && defaultParams[k] !== v) cardModified = true;
                    }

                    return (
                      <div key={testId} className={`threshold-card ${cardModified ? "has-modifications" : ""}`}>
                        <div className="threshold-card-header">
                          <h4 className="threshold-card-title">{formatTestName(testId)}</h4>
                          {cardModified && !isDefault && (
                            <button
                              type="button"
                              className="card-reset-btn"
                              onClick={() => handleResetTestCard(testId)}
                              title="Reset all fields in this card to default"
                            >
                              <RotateCcw size={13} /> Reset Card
                            </button>
                          )}
                        </div>

                        <div className="config-two-col">
                          {hasParams && (
                            <div className="config-col">
                              <p className="config-col-label">Execution Parameters</p>
                              <div className="threshold-keys">
                                {Object.entries(testParams).map(([key, value]) => {
                                  const defVal = defaultParams[key];
                                  const modified = defVal !== undefined && value !== defVal;
                                  const unit = unitForKey(key);
                                  const inputKey = `${testId}__params__${key}`;
                                  const rawVal = rawInputs[inputKey] ?? String(value);
                                  const err = inputErrors[inputKey];

                                  return (
                                    <div key={key} className={`threshold-row${modified ? " modified" : ""}`}>
                                      <span className="threshold-key-name" title={defVal !== undefined ? `Default: ${defVal}` : ""}>
                                        {formatKey(key)}
                                      </span>
                                      <div className="threshold-input-wrapper">
                                        <input
                                          type="text"
                                          className={err ? "invalid" : ""}
                                          value={rawVal}
                                          disabled={isDefault}
                                          title={defVal !== undefined ? `Default: ${defVal}` : ""}
                                          onChange={(e) =>
                                            validateAndUpdate(testId, key, e.target.value, "params")
                                          }
                                        />
                                        <span className="threshold-unit">{unit}</span>
                                        {modified && !isDefault && (
                                          <button
                                            type="button"
                                            className="reset-param-btn"
                                            onClick={() =>
                                              validateAndUpdate(testId, key, String(defVal), "params")
                                            }
                                            title={`Reset to default (${defVal})`}
                                          >
                                            <RotateCcw size={12} />
                                          </button>
                                        )}
                                      </div>
                                      {err && (
                                        <span className="threshold-error" title={err}>
                                          <AlertTriangle size={12} /> {err}
                                        </span>
                                      )}
                                    </div>
                                  );
                                })}
                              </div>
                            </div>
                          )}

                          {hasValues && (
                            <div className="config-col">
                              <p className="config-col-label">Verdict Thresholds</p>
                              <div className="threshold-keys">
                                {Object.entries(testValues).map(([key, value]) => {
                                  const defVal = defaultValues[key];
                                  const modified = defVal !== undefined && value !== defVal;
                                  const unit = unitForKey(key);
                                  const inputKey = `${testId}__values__${key}`;
                                  const rawVal = rawInputs[inputKey] ?? String(value);
                                  const err = inputErrors[inputKey];

                                  return (
                                    <div key={key} className={`threshold-row${modified ? " modified" : ""}`}>
                                      <span className="threshold-key-name" title={defVal !== undefined ? `Default: ${defVal}` : ""}>
                                        {formatKey(key)}
                                      </span>
                                      <div className="threshold-input-wrapper">
                                        <input
                                          type="text"
                                          className={err ? "invalid" : ""}
                                          value={rawVal}
                                          disabled={isDefault}
                                          title={defVal !== undefined ? `Default: ${defVal}` : ""}
                                          onChange={(e) =>
                                            validateAndUpdate(testId, key, e.target.value, "values")
                                          }
                                        />
                                        <span className="threshold-unit">{unit}</span>
                                        {modified && !isDefault && (
                                          <button
                                            type="button"
                                            className="reset-param-btn"
                                            onClick={() =>
                                              validateAndUpdate(testId, key, String(defVal), "values")
                                            }
                                            title={`Reset to default (${defVal})`}
                                          >
                                            <RotateCcw size={12} />
                                          </button>
                                        )}
                                      </div>
                                      {err && (
                                        <span className="threshold-error" title={err}>
                                          <AlertTriangle size={12} /> {err}
                                        </span>
                                      )}
                                    </div>
                                  );
                                })}
                              </div>
                            </div>
                          )}
                        </div>
                      </div>
                    );
                  })}
                </div>
              </details>
            ))}
          </div>
        )}

        {!isDefault && dirty && (
          <div className="threshold-save-bar">
            {hasErrors && (
              <span className="save-error-notice">
                <AlertTriangle size={14} /> Correct invalid inputs before saving.
              </span>
            )}
            <button className="primary" onClick={handleSave} disabled={saving || hasErrors}>
              <Save size={14} /> {saving ? "Saving..." : "Save Changes"}
            </button>
          </div>
        )}
      </div>
    </div>
  );
}
