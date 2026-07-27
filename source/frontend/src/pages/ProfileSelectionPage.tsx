import { useEffect, useMemo, useRef, useState } from "react";
import {
  Background,
  Connection,
  Controls,
  Edge,
  Handle,
  Node,
  NodeProps,
  Position,
  ReactFlow
} from "@xyflow/react";
import "@xyflow/react/dist/style.css";
import { Camera, Cable, Cpu, Download, Plus, Save, Trash2, Upload, X } from "lucide-react";
import * as api from "../api";
import {
  CameraAssignment,
  ConfirmDialogState,
  ControlDevice,
  Device,
  Profile,
  TriggerChannel,
  TriggerMode
} from "../types";

type CameraNodeData = { title: string; path: string; metadata: string };
type ChannelNodeData = { title: string; channelId: string; metadata: string; profile: string; mode: "hardware" | "software" };
type PendingImport = {
  profile: Profile;
  mode: "original" | "new";
  idDraft: string;
  nameDraft: string;
};

function CameraNode({ data }: NodeProps<Node<CameraNodeData>>) {
  return (
    <div className="routing-node camera-routing-node">
      <div className="routing-node-icon"><Camera size={20} /></div>
      <div className="routing-node-copy">
        <strong>{data.title}</strong>
        <span>{data.path}</span>
        <small>{data.metadata}</small>
      </div>
      <Handle type="source" position={Position.Right} id="camera-output" title="Assign trigger channel" />
    </div>
  );
}

function ChannelNode({ data }: NodeProps<Node<ChannelNodeData>>) {
  return (
    <div className={`routing-node channel-routing-node ${data.mode}`}>
      <Handle type="target" position={Position.Left} id="channel-input" title="Accept camera assignment" />
      <div className="routing-node-icon">{data.mode === "hardware" ? <Cable size={20} /> : <Cpu size={20} />}</div>
      <div className="routing-node-copy">
        <strong>{data.channelId}</strong>
        <span>{data.title}</span>
        <span>{data.metadata}</span>
        <small>{data.profile}</small>
      </div>
    </div>
  );
}

const nodeTypes = { camera: CameraNode, channel: ChannelNode };

type Props = {
  devices: Device[];
  profiles: Profile[];
  triggerMode: TriggerMode;
  onTriggerModeChange: (mode: TriggerMode) => void;
  assignmentMode: "single" | "per-camera";
  onAssignmentModeChange: (mode: "single" | "per-camera") => void;
  singleProfileId: string;
  onSingleProfileChange: (id: string) => void;
  assignments: CameraAssignment[];
  onAssignmentsChange: (assignments: CameraAssignment[]) => void;
  onProfilesChanged: () => Promise<void>;
  onError: (message: string | null) => void;
  onSuccess: (message: string) => void;
  requestConfirm: (state: Omit<ConfirmDialogState, "onConfirm"> & { onConfirm: () => void }) => void;
};

function channelTitle(channel: TriggerChannel) {
  if (channel.name) return channel.name;
  if (channel.type === "hardware" && channel.gpio) {
    return `gpiochip${channel.gpio.chip_id} · line ${channel.gpio.line_number}`;
  }
  return channel.fire?.[0]?.name || "Software control";
}

function channelMetadata(channel: TriggerChannel) {
  if (channel.type === "hardware" && channel.gpio) {
    const identity = `gpiochip${channel.gpio.chip_id} · line ${channel.gpio.line_number}`;
    const description = channel.gpio.description && channel.gpio.description !== channel.name
      ? channel.gpio.description
      : "";
    return description ? `${identity} · ${description}` : identity;
  }
  const selector = channel.control_device;
  return selector?.kind === "capture"
    ? "Selected camera control node"
    : selector?.sysfs_name || selector?.card || selector?.driver || "V4L2 control device";
}

function matcherMatches(device: Device, matcher: { driver: string; card: string; bus_info: string }) {
  return (!matcher.driver || matcher.driver === device.driver) &&
    (!matcher.card || matcher.card === device.card) &&
    (!matcher.bus_info || matcher.bus_info === device.bus_info);
}

function compatibleChannels(profile: Profile, mode: TriggerMode) {
  return mode === "free-run" ? [] : profile.trigger_channels.filter((channel) => channel.type === mode);
}

function sanitizeProfileId(value: string) {
  return value.toLowerCase().replace(/[^a-z0-9_-]/g, "-");
}

export function ProfileSelectionPage({
  devices,
  profiles,
  triggerMode,
  onTriggerModeChange,
  assignmentMode,
  onAssignmentModeChange,
  singleProfileId,
  onSingleProfileChange,
  assignments,
  onAssignmentsChange,
  onProfilesChanged,
  onError,
  onSuccess,
  requestConfirm
}: Props) {
  const [compactRouting, setCompactRouting] = useState(false);
  const [showCreate, setShowCreate] = useState(false);
  const [pendingDelete, setPendingDelete] = useState(false);
  const [pendingImport, setPendingImport] = useState<PendingImport | null>(null);
  const importFileRef = useRef<HTMLInputElement>(null);
  const [controlDevices, setControlDevices] = useState<ControlDevice[]>([]);

  const selectedProfile = profiles.find((p) => p.id === singleProfileId);
  const [triggerRateDraft, setTriggerRateDraft] = useState(String(selectedProfile?.defaults.trigger_rate_hz ?? 30));
  const [pulseWidthDraft, setPulseWidthDraft] = useState(String(selectedProfile?.defaults.pulse_width_ms ?? 13));
  const timingDirty = Boolean(selectedProfile) &&
    (Number(triggerRateDraft) !== selectedProfile?.defaults.trigger_rate_hz ||
      Number(pulseWidthDraft) !== selectedProfile?.defaults.pulse_width_ms);
  useEffect(() => {
    setTriggerRateDraft(String(selectedProfile?.defaults.trigger_rate_hz ?? 30));
    setPulseWidthDraft(String(selectedProfile?.defaults.pulse_width_ms ?? 13));
  }, [selectedProfile?.id, selectedProfile?.defaults.trigger_rate_hz, selectedProfile?.defaults.pulse_width_ms]);
  const [profileId, setProfileId] = useState("");
  const [profileName, setProfileName] = useState("");
  const [description, setDescription] = useState("");
  const [channelId, setChannelId] = useState("channel-0");
  const [channelName, setChannelName] = useState("");
  const [chipId, setChipId] = useState(0);
  const [lineNumber, setLineNumber] = useState(0);
  const [controlDevicePath, setControlDevicePath] = useState("");
  const [fireControlId, setFireControlId] = useState(0);
  const [fireValue, setFireValue] = useState(0);
  const [setupControlId, setSetupControlId] = useState(0);
  const [setupValue, setSetupValue] = useState(0);
  const [teardownControlId, setTeardownControlId] = useState(0);
  const [teardownValue, setTeardownValue] = useState(0);

  useEffect(() => {
    const media = window.matchMedia("(max-width: 980px)");
    const update = () => setCompactRouting(media.matches);
    update();
    media.addEventListener("change", update);
    return () => media.removeEventListener("change", update);
  }, []);

  useEffect(() => {
    if (!showCreate || triggerMode !== "software") return;
    api.getControlDevices()
      .then(async (response) => {
        if (!response.ok) throw new Error("Failed to discover V4L2 controls.");
        const json = await response.json();
        setControlDevices(json.devices ?? []);
      })
      .catch((error: Error) => onError(error.message));
  }, [showCreate, triggerMode, onError]);

  const visibleProfiles = useMemo(
    () => profiles.filter((profile) => compatibleChannels(profile, triggerMode).length > 0),
    [profiles, triggerMode]
  );

  const visibleChannels = useMemo(() => {
    const selectedProfiles = assignmentMode === "single"
      ? profiles.filter((profile) => profile.id === singleProfileId)
      : visibleProfiles;
    return selectedProfiles.flatMap((profile) =>
      compatibleChannels(profile, triggerMode).map((channel) => ({ profile, channel }))
    );
  }, [assignmentMode, profiles, singleProfileId, triggerMode, visibleProfiles]);

  const nodes = useMemo(() => {
    const cameraNodes: Node<CameraNodeData>[] = devices.map((device, index) => ({
      id: `camera:${device.path}`,
      type: "camera",
      position: { x: compactRouting ? 12 : 24, y: 40 + index * (compactRouting ? 100 : 118) },
      draggable: false,
      data: {
        title: device.card || device.driver || device.path,
        path: device.path,
        metadata: [device.driver, device.bus_info].filter(Boolean).join(" · ")
      }
    }));
    const channelNodes: Node<ChannelNodeData>[] = visibleChannels.map(({ profile, channel }, index) => ({
      id: `channel:${profile.id}:${channel.id}`,
      type: "channel",
      position: { x: compactRouting ? 172 : 570, y: 40 + index * (compactRouting ? 100 : 118) },
      draggable: false,
      data: {
        title: channelTitle(channel),
        channelId: channel.id,
        metadata: channelMetadata(channel),
        profile: profile.name,
        mode: channel.type
      }
    }));
    return [...cameraNodes, ...channelNodes];
  }, [compactRouting, devices, visibleChannels]);

  const edges = useMemo<Edge[]>(() => assignments.flatMap((assignment) => {
    const target = visibleChannels.find(({ profile, channel }) =>
      profile.id === assignment.profile_id && channel.id === assignment.trigger_channel_id
    );
    if (!target || !devices.some((device) => device.path === assignment.path)) return [];
    return [{
      id: `route:${assignment.path}`,
      source: `camera:${assignment.path}`,
      target: `channel:${target.profile.id}:${target.channel.id}`,
      sourceHandle: "camera-output",
      targetHandle: "channel-input",
      type: "smoothstep",
      animated: false,
      style: { stroke: triggerMode === "software" ? "#f59e0b" : "#3b82f6", strokeWidth: 2.5 }
    }];
  }), [assignments, devices, triggerMode, visibleChannels]);

  function applySingleProfile(id: string) {
    onSingleProfileChange(id);
    const profile = profiles.find((item) => item.id === id);
    if (!profile) {
      onAssignmentsChange(devices.map((device) => ({ path: device.path, profile_id: "", trigger_channel_id: "" })));
      return;
    }
    const channels = compatibleChannels(profile, triggerMode);
    onAssignmentsChange(devices.map((device) => {
      const binding = profile.camera_bindings.find((item) => matcherMatches(device, item.camera));
      const selectedChannel = binding && channels.some((channel) => channel.id === binding.trigger_channel_id)
        ? binding.trigger_channel_id
        : channels.length === 1 ? channels[0].id : "";
      return { path: device.path, profile_id: profile.id, trigger_channel_id: selectedChannel };
    }));
  }

  function handleSelectedProfileChange(id: string) {
    if (assignmentMode === "single") {
      applySingleProfile(id);
      return;
    }
    onSingleProfileChange(id);
  }

  function connect(connection: Connection) {
    if (!connection.source?.startsWith("camera:") || !connection.target?.startsWith("channel:")) return;
    const cameraPath = connection.source.slice("camera:".length);
    const channelTarget = visibleChannels.find(({ profile, channel }) =>
      `channel:${profile.id}:${channel.id}` === connection.target
    );
    if (!channelTarget) return;
    const next = assignments.filter((assignment) => assignment.path !== cameraPath);
    next.push({
      path: cameraPath,
      profile_id: channelTarget.profile.id,
      trigger_channel_id: channelTarget.channel.id
    });
    onAssignmentsChange(next);
  }

  function deleteEdges(deleted: Edge[]) {
    const removedPaths = new Set(deleted.map((edge) => edge.source.slice("camera:".length)));
    onAssignmentsChange(assignments.map((assignment) =>
      removedPaths.has(assignment.path) ? { ...assignment, profile_id: "", trigger_channel_id: "" } : assignment
    ));
  }

  function resetRouting() {
    onAssignmentsChange(devices.map((device) => ({ path: device.path, profile_id: "", trigger_channel_id: "" })));
    onSuccess("Routing cleared.");
  }

  async function saveRouting() {
    const updates = profiles.filter((profile) => assignments.some((assignment) => assignment.profile_id === profile.id));
    if (updates.length === 0) {
      onError("Assign at least one camera before saving routing.");
      return;
    }
    for (const profile of updates) {
      const newBindings = assignments
        .filter((assignment) => assignment.profile_id === profile.id)
        .map((assignment) => {
          const device = devices.find((item) => item.path === assignment.path)!;
          return {
            camera: { driver: device.driver, card: device.card, bus_info: device.bus_info },
            trigger_channel_id: assignment.trigger_channel_id
          };
        });
      const response = await api.updateProfile({ ...profile, camera_bindings: newBindings });
      if (!response.ok) {
        const json = await response.json();
        throw new Error(json.error ?? "Failed to save routing.");
      }
    }
    await onProfilesChanged();
    onError(null);
    onSuccess(updates.length === 1 ? `Routing saved to ${updates[0].name}.` : `Routing saved to ${updates.length} profiles.`);
  }

  async function applyTriggerTiming() {
    if (!selectedProfile) return;
    const rate = Number(triggerRateDraft);
    const pulse = Number(pulseWidthDraft);
    if (!Number.isFinite(rate) || rate <= 0) {
      onError("Trigger rate must be a number greater than 0.");
      return;
    }
    if (!Number.isFinite(pulse) || pulse <= 0) {
      onError("Pulse width must be a number greater than 0.");
      return;
    }
    const updated = { ...selectedProfile, defaults: { ...selectedProfile.defaults, trigger_rate_hz: rate, pulse_width_ms: pulse } };
    const response = await api.updateProfile(updated);
    if (!response.ok) {
      const json = await response.json();
      throw new Error(json.error ?? "Failed to update trigger timing.");
    }
    await onProfilesChanged();
    onError(null);
    onSuccess(`Trigger timing saved to ${selectedProfile.name}.`);
  }

  function selectedControl(controlId = fireControlId) {
    return controlDevices.find((device) => device.path === controlDevicePath)?.controls
      .find((control) => control.id === controlId);
  }

  async function createProfile() {
    const controlDevice = controlDevices.find((device) => device.path === controlDevicePath);
    const control = selectedControl();
    const setupControl = selectedControl(setupControlId);
    const teardownControl = selectedControl(teardownControlId);
    const channel: TriggerChannel = triggerMode === "hardware"
      ? {
          id: channelId,
          name: channelName,
          description: "",
          type: "hardware",
          gpio: { chip_id: chipId, line_number: lineNumber, description: channelName }
        }
      : {
          id: channelId,
          name: channelName,
          description: "",
          type: "software",
          control_device: controlDevice?.path.startsWith("/dev/v4l-subdev")
            ? {
                kind: "subdevice",
                driver: controlDevice.driver,
                card: controlDevice.card,
                bus_info: controlDevice.bus_info,
                sysfs_name: controlDevice.sysfs_name
              }
            : devices.some((device) => device.path === controlDevice?.path)
              ? { kind: "capture", driver: "", card: "", bus_info: "", sysfs_name: "" }
              : {
                  kind: "video",
                  driver: controlDevice?.driver ?? "",
                  card: controlDevice?.card ?? "",
                  bus_info: controlDevice?.bus_info ?? "",
                  sysfs_name: controlDevice?.sysfs_name ?? ""
                },
          setup: setupControl ? [{ id: setupControl.id, name: setupControl.name, type: setupControl.type, value: setupValue }] : [],
          fire: control ? [{ id: control.id, name: control.name, type: control.type, value: fireValue }] : [],
          teardown: teardownControl ? [{ id: teardownControl.id, name: teardownControl.name, type: teardownControl.type, value: teardownValue }] : []
        };
    const profile: Profile = {
      schema_version: 2,
      id: profileId,
      name: profileName,
      description,
      enabled: true,
      camera_match: { driver: "", card: "", bus_info: "" },
      defaults: {
        trigger_mode: triggerMode,
        memory_backends: [],
        test_selectors: ["implemented"],
        report_formats: ["json", "html"],
        trigger_rate_hz: 30,
        pulse_width_ms: 13
      },
      trigger_channels: [channel],
      camera_bindings: []
    };
    const response = await api.createProfile(profile);
    if (!response.ok) {
      const json = await response.json();
      throw new Error(json.error ?? "Failed to create profile.");
    }
    await onProfilesChanged();
    onSingleProfileChange(profile.id);
    setShowCreate(false);
    onError(null);
    onSuccess(`Profile ${profile.name} created.`);
  }

  async function removeSelectedProfile() {
    if (!singleProfileId) return;
    const response = await api.deleteProfile(singleProfileId);
    if (!response.ok) {
      const json = await response.json();
      throw new Error(json.error ?? "Failed to delete profile.");
    }
    resetRouting();
    setPendingDelete(false);
    await onProfilesChanged();
    onSuccess("Profile deleted.");
  }

  async function testSelectedRouting() {
    const assignment = assignments.find((item) => item.profile_id && item.trigger_channel_id);
    if (!assignment) return;
    const response = await api.testSoftwareTrigger({
      camera_path: assignment.path,
      profile_id: assignment.profile_id,
      trigger_channel_id: assignment.trigger_channel_id
    });
    const json = await response.json();
    if (!response.ok) throw new Error(json.error ?? "Software trigger test failed.");
    onError(null);
    onSuccess("Software trigger fired.");
  }

  async function handleExportProfile() {
    if (!singleProfileId) return;
    const res = await api.exportProfile(singleProfileId);
    if (!res.ok) throw new Error("Failed to export profile.");
    const text = await res.text();
    const blob = new Blob([text], { type: "application/json" });
    const url = URL.createObjectURL(blob);
    const a = document.createElement("a");
    a.href = url;
    a.download = `${singleProfileId}.profile.json`;
    a.click();
    URL.revokeObjectURL(url);
    onSuccess("Profile exported.");
  }

  async function handleImportProfile(e: React.ChangeEvent<HTMLInputElement>) {
    const file = e.target.files?.[0];
    if (!file) return;
    try {
      const text = await file.text();
      const parsed = JSON.parse(text) as Profile;
      setPendingImport({
        profile: parsed,
        mode: "original",
        idDraft: sanitizeProfileId(`${parsed.id || "imported-profile"}-copy`),
        nameDraft: parsed.name ? `${parsed.name} copy` : "Imported profile"
      });
      onError(null);
    } catch {
      onError("Import failed. The selected file is not valid JSON.");
    } finally {
      e.target.value = "";
    }
  }

  async function savePendingImport() {
    if (!pendingImport) return;
    const originalId = sanitizeProfileId(pendingImport.profile.id);
    const nextProfile = pendingImport.mode === "new"
      ? {
          ...pendingImport.profile,
          id: sanitizeProfileId(pendingImport.idDraft),
          name: pendingImport.nameDraft.trim()
        }
      : {
          ...pendingImport.profile,
          id: originalId
        };
    if (!nextProfile.id || !nextProfile.name) {
      onError("Profile ID and name are required.");
      return;
    }
    const exists = profiles.some((profile) => profile.id === nextProfile.id);
    if (pendingImport.mode === "new" && exists) {
      onError("A profile with this ID already exists.");
      return;
    }
    const res = exists ? await api.updateProfile(nextProfile) : await api.createProfile(nextProfile);
    if (!res.ok) {
      const json = await res.json();
      throw new Error(json.error ?? "Import failed.");
    }
    await onProfilesChanged();
    onSingleProfileChange(nextProfile.id);
    setPendingImport(null);
    onError(null);
    onSuccess(exists ? `Profile ${nextProfile.name} updated.` : `Profile ${nextProfile.name} imported.`);
  }

  const writableControls = controlDevices.find((device) => device.path === controlDevicePath)?.controls
    .filter((control) => control.supported_for_trigger) ?? [];
  const routedCount = triggerMode === "free-run" ? devices.length : assignments.filter((item) => item.trigger_channel_id).length;

  return (
    <div className="page">
      <header className="topbar">
        <div><p className="eyebrow">Configure</p><h2>Trigger Routing</h2></div>
        <div className="toolbar-row">
          <div className="segmented-control" aria-label="Trigger mode">
            {(["hardware", "software", "free-run"] as TriggerMode[]).map((mode) => (
              <button key={mode} className={triggerMode === mode ? "selected" : ""} onClick={() => onTriggerModeChange(mode)}>
                {mode === "free-run" ? "Free-run" : mode[0].toUpperCase() + mode.slice(1)}
              </button>
            ))}
          </div>
        </div>
      </header>

      <section className="routing-toolbar">
        <div className="segmented-control" aria-label="Profile assignment mode">
          <button className={assignmentMode === "single" ? "selected" : ""} onClick={() => onAssignmentModeChange("single")}>Single profile</button>
          <button className={assignmentMode === "per-camera" ? "selected" : ""} onClick={() => onAssignmentModeChange("per-camera")}>Per camera</button>
        </div>
        {triggerMode !== "free-run" && (
          <select value={singleProfileId} onChange={(event) => handleSelectedProfileChange(event.target.value)}>
            <option value="">Select profile</option>
            {visibleProfiles.map((profile) => <option key={profile.id} value={profile.id}>{profile.name}</option>)}
          </select>
        )}
        {triggerMode !== "free-run" && (
          <div className="trigger-timing-compact" aria-label="Trigger timing">
            <span className="timing-label">Timing</span>
            <label>
              <input type="number" min="1" max="1000" step="1" value={triggerRateDraft}
                     disabled={!selectedProfile}
                     onChange={(e) => setTriggerRateDraft(e.target.value)} />
              <span>Hz</span>
            </label>
            <label>
              <input type="number" min="1" max="100" step="0.5" value={pulseWidthDraft}
                     disabled={!selectedProfile}
                     onChange={(e) => setPulseWidthDraft(e.target.value)} />
              <span>ms</span>
            </label>
            <button className="icon-text-button timing-save"
                    disabled={!selectedProfile || !timingDirty}
                    onClick={() => applyTriggerTiming().catch((error: Error) => onError(error.message))}>
              <Save size={14} /> Apply
            </button>
          </div>
        )}
        <button className="icon-text-button" onClick={() => setShowCreate(true)} disabled={triggerMode === "free-run"}>
          <Plus size={16} /> New profile
        </button>
        <button className="icon-button" title="Export selected profile" onClick={() => handleExportProfile().catch((e: Error) => onError(e.message))} disabled={!singleProfileId}>
          <Download size={16} />
        </button>
        <button className="icon-button" title="Import profile" onClick={() => importFileRef.current?.click()}>
          <Upload size={16} />
        </button>
        <input ref={importFileRef} type="file" accept=".json" style={{ display: "none" }} onChange={(e) => handleImportProfile(e).catch((err: Error) => onError(err.message))} />
        <button className="icon-button danger" title="Delete selected profile" onClick={() => setPendingDelete(true)} disabled={!singleProfileId}>
          <Trash2 size={16} />
        </button>
      </section>

      {pendingDelete && (
        <section className="inline-confirm" role="alertdialog" aria-label="Delete profile">
          <span>Delete the selected local profile?</span>
          <button onClick={() => setPendingDelete(false)}>Cancel</button>
          <button className="danger-button" onClick={() => removeSelectedProfile().catch((error: Error) => onError(error.message))}>Delete</button>
        </section>
      )}

      {pendingImport && (
        <div className="dialog-overlay">
          <section className="dialog import-dialog" role="dialog" aria-label="Import profile">
            <h3>Import Profile</h3>
            <p>Choose how to save {pendingImport.profile.name || pendingImport.profile.id || "this profile"}.</p>
            <div className="import-mode-row">
              <button
                className={pendingImport.mode === "original" ? "selected" : ""}
                onClick={() => setPendingImport({ ...pendingImport, mode: "original" })}
              >
                Use file name
              </button>
              <button
                className={pendingImport.mode === "new" ? "selected" : ""}
                onClick={() => setPendingImport({ ...pendingImport, mode: "new" })}
              >
                Save as new
              </button>
            </div>
            {pendingImport.mode === "original" ? (
              <div className="import-summary">
                <strong>{pendingImport.profile.name || pendingImport.profile.id}</strong>
                <span>
                  {profiles.some((profile) => profile.id === sanitizeProfileId(pendingImport.profile.id))
                    ? "This will update the existing profile with the same ID."
                    : "This will import the profile with the ID from the file."}
                </span>
              </div>
            ) : (
              <div className="profile-form-grid import-fields">
                <label>Profile ID<input value={pendingImport.idDraft} onChange={(event) => setPendingImport({ ...pendingImport, idDraft: sanitizeProfileId(event.target.value) })} /></label>
                <label>Name<input value={pendingImport.nameDraft} onChange={(event) => setPendingImport({ ...pendingImport, nameDraft: event.target.value })} /></label>
              </div>
            )}
            <div className="dialog-actions">
              <button className="dialog-cancel" onClick={() => setPendingImport(null)}>Cancel</button>
              <button className="dialog-confirm primary" onClick={() => savePendingImport().catch((error: Error) => onError(error.message))}>
                {pendingImport.mode === "original" ? "Save" : "Save new"}
              </button>
            </div>
          </section>
        </div>
      )}

      {showCreate && (
        <section className="profile-editor">
          <div className="panel-title"><Plus size={18} /><h3>New {triggerMode} profile</h3><button className="icon-button editor-close" title="Close" onClick={() => setShowCreate(false)}><X size={16} /></button></div>
          <div className="profile-form-grid">
            <label>Profile ID<input value={profileId} onChange={(event) => setProfileId(event.target.value.toLowerCase().replace(/[^a-z0-9_-]/g, "-"))} /></label>
            <label>Name<input value={profileName} onChange={(event) => setProfileName(event.target.value)} /></label>
            <label className="wide-field">Description<input value={description} onChange={(event) => setDescription(event.target.value)} /></label>
            <label>Channel ID<input value={channelId} onChange={(event) => setChannelId(event.target.value.toLowerCase().replace(/[^a-z0-9_-]/g, "-"))} /></label>
            <label>Channel label<input value={channelName} onChange={(event) => setChannelName(event.target.value)} /></label>
            {triggerMode === "hardware" ? (
              <>
                <label>GPIO chip<input type="number" min="0" value={chipId} onChange={(event) => setChipId(Number(event.target.value))} /></label>
                <label>GPIO line<input type="number" min="0" value={lineNumber} onChange={(event) => setLineNumber(Number(event.target.value))} /></label>
              </>
            ) : (
              <>
                <label className="wide-field">Control device<select value={controlDevicePath} onChange={(event) => { setControlDevicePath(event.target.value); setFireControlId(0); }}><option value="">Select device</option>{controlDevices.map((device) => <option key={device.path} value={device.path}>{device.card || device.sysfs_name || device.path} · {device.path}</option>)}</select></label>
                <label>Fire control<select value={fireControlId} onChange={(event) => { const id = Number(event.target.value); setFireControlId(id); const next = writableControls.find((control) => control.id === id); setFireValue(next?.type === 4 ? 0 : next?.current_value ?? 0); }}><option value="0">Select control</option>{writableControls.map((control) => <option key={control.id} value={control.id}>{control.name}</option>)}</select></label>
                <label>Fire value<input type="number" value={fireValue} disabled={selectedControl()?.type === 4} onChange={(event) => setFireValue(Number(event.target.value))} /></label>
                <label>Setup control<select value={setupControlId} onChange={(event) => { const id = Number(event.target.value); setSetupControlId(id); setSetupValue(selectedControl(id)?.current_value ?? 0); }}><option value="0">None</option>{writableControls.map((control) => <option key={control.id} value={control.id}>{control.name}</option>)}</select></label>
                <label>Setup value<input type="number" value={setupValue} disabled={!setupControlId || selectedControl(setupControlId)?.type === 4} onChange={(event) => setSetupValue(Number(event.target.value))} /></label>
                <label>Teardown control<select value={teardownControlId} onChange={(event) => { const id = Number(event.target.value); setTeardownControlId(id); setTeardownValue(selectedControl(id)?.current_value ?? 0); }}><option value="0">None</option>{writableControls.map((control) => <option key={control.id} value={control.id}>{control.name}</option>)}</select></label>
                <label>Teardown value<input type="number" value={teardownValue} disabled={!teardownControlId || selectedControl(teardownControlId)?.type === 4} onChange={(event) => setTeardownValue(Number(event.target.value))} /></label>
              </>
            )}
          </div>
          <div className="editor-actions"><button onClick={() => setShowCreate(false)}>Cancel</button><button className="primary" onClick={() => createProfile().catch((error: Error) => onError(error.message))}><Save size={16} /> Save profile</button></div>
        </section>
      )}

      <section className="routing-canvas-section">
        {devices.length === 0 ? (
          <div className="empty">Select cameras before configuring trigger routing.</div>
        ) : triggerMode === "free-run" ? (
          <div className="free-run-grid">{devices.map((device) => <div key={device.path} className="free-run-device"><Camera size={20} /><div><strong>{device.card || device.driver || device.path}</strong><span>{device.path}</span></div><span className="free-run-badge">Free-run</span></div>)}</div>
        ) : visibleChannels.length === 0 ? (
          <div className="empty">No compatible trigger channels are available. Create or select a profile.</div>
        ) : (
          <ReactFlow
            className={compactRouting ? "compact-routing" : ""}
            nodes={nodes}
            edges={edges}
            nodeTypes={nodeTypes}
            onConnect={connect}
            onEdgesDelete={deleteEdges}
            isValidConnection={(connection) => connection.source?.startsWith("camera:") === true && connection.target?.startsWith("channel:") === true}
            connectOnClick
            nodesDraggable={false}
            nodesConnectable
            elementsSelectable
            fitView
            minZoom={compactRouting ? 0.8 : 0.6}
            maxZoom={1.4}
          >
            <Background color="#2a3446" gap={24} size={1} />
            <Controls showInteractive={false} />
          </ReactFlow>
        )}
        <footer className="routing-footer">
          <span>{routedCount} cameras routed · {Math.max(0, devices.length - routedCount)} unassigned</span>
          <div>
            <button onClick={() => requestConfirm({ title: "Reset Routing", message: "Clear all camera routing assignments?", confirmLabel: "Reset", variant: "danger", onConfirm: resetRouting })}>Reset</button>
            {triggerMode === "software" && <button onClick={() => testSelectedRouting().catch((error: Error) => onError(error.message))} disabled={!assignments.some((item) => item.trigger_channel_id)}>Test trigger</button>}
            <button onClick={() => requestConfirm({ title: "Save Routing", message: "Save current routing configuration to the selected profile(s)?", confirmLabel: "Save", variant: "primary", onConfirm: () => saveRouting().catch((error: Error) => onError(error.message)) })} disabled={triggerMode === "free-run" || !assignments.some((item) => item.trigger_channel_id)}><Save size={16} /> Save routing</button>
          </div>
        </footer>
      </section>
    </div>
  );
}
