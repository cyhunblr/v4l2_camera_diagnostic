import { useCallback, useEffect, useRef, useState } from "react";
import { SlidersHorizontal, Download, Upload, Plus, Trash2, Save } from "lucide-react";
import { ThresholdConfig } from "../types";
import * as api from "../api";

/** Human-readable test names derived from test ids. */
function formatTestName(testId: string): string {
  // "t07-poll-timeout-sweep" → "t07 Poll Timeout Sweep"
  const parts = testId.split("-");
  const num = parts[0]; // e.g. "t07"
  const words = parts.slice(1).map((w) => w.charAt(0).toUpperCase() + w.slice(1));
  return `${num} ${words.join(" ")}`;
}

/** Infer unit from key name suffix. */
function unitForKey(key: string): string {
  if (key.endsWith("_ms")) return "ms";
  if (key.endsWith("_pct")) return "%";
  return "count";
}

/** Format key for display: remove unit suffix, replace _ with spaces. */
function formatKey(key: string): string {
  return key
    .replace(/_ms$/, "")
    .replace(/_pct$/, "")
    .replace(/_/g, " ");
}

type Props = {
  selectedThresholdId: string;
  onSelectedChange: (id: string) => void;
  onError: (msg: string) => void;
};

export function ThresholdConfigPage({ selectedThresholdId, onSelectedChange, onError }: Props) {
  const [configs, setConfigs] = useState<ThresholdConfig[]>([]);
  const [editing, setEditing] = useState<ThresholdConfig | null>(null);
  const [dirty, setDirty] = useState(false);
  const [saving, setSaving] = useState(false);
  const [newId, setNewId] = useState("");
  const [showNew, setShowNew] = useState(false);
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
    } else if (configs.length > 0) {
      onSelectedChange(configs[0].id);
    }
  }, [selectedThresholdId, configs, onSelectedChange]);

  function handleValueChange(testId: string, key: string, value: string) {
    if (!editing) return;
    const num = parseFloat(value);
    if (isNaN(num)) return;
    const next = structuredClone(editing);
    if (!next.values[testId]) next.values[testId] = {};
    next.values[testId][key] = num;
    setEditing(next);
    setDirty(true);
  }

  function handleParamChange(testId: string, key: string, value: string) {
    if (!editing) return;
    const num = parseFloat(value);
    if (isNaN(num)) return;
    const next = structuredClone(editing);
    if (!next.params) next.params = {};
    if (!next.params[testId]) next.params[testId] = {};
    next.params[testId][key] = num;
    setEditing(next);
    setDirty(true);
  }

  async function handleSave() {
    if (!editing || !dirty) return;
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
  // Collect all test IDs from both values and params
  const allTestIds = editing
    ? [...new Set([...Object.keys(editing.values), ...Object.keys(editing.params ?? {})])].sort()
    : [];

  return (
    <div className="page">
      <header className="topbar">
        <div>
          <p className="eyebrow">Configure</p>
          <h2>Test Configuration</h2>
        </div>
      </header>

      <div className="panel">
        <div className="panel-title">
          <SlidersHorizontal size={18} />
          <h3>Parameters &amp; Verdicts</h3>
        </div>

        <div className="threshold-toolbar">
          <select
            value={selectedThresholdId}
            onChange={(e) => onSelectedChange(e.target.value)}
          >
            {configs.map((c) => (
              <option key={c.id} value={c.id}>
                {c.name} {c.id === "default" ? "(built-in)" : ""}
              </option>
            ))}
          </select>

          <button className="icon-btn" onClick={() => setShowNew(true)} title="New config">
            <Plus size={16} />
          </button>
          <button className="icon-btn" onClick={handleExport} title="Export" disabled={!editing}>
            <Download size={16} />
          </button>
          <button className="icon-btn" onClick={handleImportClick} title="Import">
            <Upload size={16} />
          </button>
          {!isDefault && (
            <button className="icon-btn danger" onClick={handleDelete} title="Delete">
              <Trash2 size={16} />
            </button>
          )}
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
            <button onClick={handleCreate}>Create</button>
            <button onClick={() => setShowNew(false)}>Cancel</button>
          </div>
        )}

        {editing && (
          <div className="threshold-editor">
            {isDefault && (
              <p className="threshold-hint">
                The default config is read-only. Create a new config to customize values.
              </p>
            )}

            {allTestIds.map((testId) => {
              const defaultValues = defaultConfig?.values[testId] ?? {};
              const defaultParams = defaultConfig?.params?.[testId] ?? {};
              const testValues = editing.values[testId] ?? {};
              const testParams = editing.params?.[testId] ?? {};
              const hasParams = Object.keys(testParams).length > 0 || Object.keys(defaultParams).length > 0;
              const hasValues = Object.keys(testValues).length > 0 || Object.keys(defaultValues).length > 0;

              return (
                <div key={testId} className="threshold-card">
                  <h4 className="threshold-card-title">{formatTestName(testId)}</h4>
                  <div className="config-two-col">
                    {hasParams && (
                      <div className="config-col">
                        <p className="config-col-label">Parameters</p>
                        <div className="threshold-keys">
                          {Object.entries(testParams).map(([key, value]) => {
                            const defVal = defaultParams[key];
                            const modified = defVal !== undefined && value !== defVal;
                            const unit = unitForKey(key);
                            return (
                              <div key={key} className={`threshold-row${modified ? " modified" : ""}`}>
                                <span className="threshold-key-name">{formatKey(key)}</span>
                                <input
                                  type="number"
                                  step="any"
                                  value={value}
                                  disabled={isDefault}
                                  onChange={(e) => handleParamChange(testId, key, e.target.value)}
                                />
                                <span className="threshold-unit">{unit}</span>
                                {defVal !== undefined && (
                                  <span className="threshold-default">default: {defVal}</span>
                                )}
                                {modified && <span className="threshold-modified-dot" title="Modified from default">●</span>}
                              </div>
                            );
                          })}
                        </div>
                      </div>
                    )}
                    {hasValues && (
                      <div className="config-col">
                        <p className="config-col-label">Verdicts</p>
                        <div className="threshold-keys">
                          {Object.entries(testValues).map(([key, value]) => {
                            const defVal = defaultValues[key];
                            const modified = defVal !== undefined && value !== defVal;
                            const unit = unitForKey(key);
                            return (
                              <div key={key} className={`threshold-row${modified ? " modified" : ""}`}>
                                <span className="threshold-key-name">{formatKey(key)}</span>
                                <input
                                  type="number"
                                  step="any"
                                  value={value}
                                  disabled={isDefault}
                                  onChange={(e) => handleValueChange(testId, key, e.target.value)}
                                />
                                <span className="threshold-unit">{unit}</span>
                                {defVal !== undefined && (
                                  <span className="threshold-default">default: {defVal}</span>
                                )}
                                {modified && <span className="threshold-modified-dot" title="Modified from default">●</span>}
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
        )}

        {!isDefault && dirty && (
          <div className="threshold-save-bar">
            <button className="primary" onClick={handleSave} disabled={saving}>
              <Save size={14} /> {saving ? "Saving..." : "Save Changes"}
            </button>
          </div>
        )}
      </div>
    </div>
  );
}
