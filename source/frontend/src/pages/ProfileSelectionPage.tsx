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
  ControlDevice,
  Device,
  Profile,
  TriggerChannel,
  TriggerMode
} from "../types";

const BACKEND_OPTIONS = ["mmap", "dmabuf", "userptr"];

type CameraNodeData = { title: string; path: string; metadata: string };
type ChannelNodeData = { title: string; metadata: string; profile: string; mode: "hardware" | "software" };

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
        <strong>{data.title}</strong>
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
  backends: string[];
  onToggleBackend: (backend: string) => void;
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
    return `gpiochip${channel.gpio.chip_id} · line ${channel.gpio.line_number}`;
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
  backends,
  onToggleBackend
}: Props) {
  const [compactRouting, setCompactRouting] = useState(false);
  const [showCreate, setShowCreate] = useState(false);
  const [pendingDelete, setPendingDelete] = useState(false);
  const importFileRef = useRef<HTMLInputElement>(null);
  const [controlDevices, setControlDevices] = useState<ControlDevice[]>([]);
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
    if (!profile) return;
    const channels = compatibleChannels(profile, triggerMode);
    onAssignmentsChange(devices.map((device) => {
      const binding = profile.camera_bindings.find((item) => matcherMatches(device, item.camera));
      const selectedChannel = binding && channels.some((channel) => channel.id === binding.trigger_channel_id)
        ? binding.trigger_channel_id
        : channels.length === 1 ? channels[0].id : "";
      return { path: device.path, profile_id: profile.id, trigger_channel_id: selectedChannel };
    }));
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
  }

  async function saveRoutingDefaults() {
    const updates = profiles.filter((profile) => assignments.some((assignment) => assignment.profile_id === profile.id));
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
        throw new Error(json.error ?? "Failed to save routing defaults.");
      }
    }
    await onProfilesChanged();
    onError(null);
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
        memory_backends: backends,
        test_selectors: ["implemented"],
        report_formats: ["json", "html"]
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
  }

  async function handleImportProfile(e: React.ChangeEvent<HTMLInputElement>) {
    const file = e.target.files?.[0];
    if (!file) return;
    const text = await file.text();
    const res = await api.importProfile(text);
    if (!res.ok) {
      const json = await res.json();
      throw new Error(json.error ?? "Import failed.");
    }
    await onProfilesChanged();
    e.target.value = "";
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
        {triggerMode !== "free-run" && assignmentMode === "single" && (
          <select value={singleProfileId} onChange={(event) => applySingleProfile(event.target.value)}>
            <option value="">Select profile</option>
            {visibleProfiles.map((profile) => <option key={profile.id} value={profile.id}>{profile.name}</option>)}
          </select>
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
            <button onClick={resetRouting}>Reset</button>
            {triggerMode === "software" && <button onClick={() => testSelectedRouting().catch((error: Error) => onError(error.message))} disabled={!assignments.some((item) => item.trigger_channel_id)}>Test trigger</button>}
            <button onClick={() => saveRoutingDefaults().catch((error: Error) => onError(error.message))} disabled={triggerMode === "free-run"}><Save size={16} /> Save as profile default</button>
          </div>
        </footer>
      </section>

      <section className="panel">
        <div className="panel-title"><h3>Memory Backend</h3></div>
        <div className="choice-row">{BACKEND_OPTIONS.map((backend) => <button key={backend} className={backends.includes(backend) ? "selected" : ""} onClick={() => onToggleBackend(backend)}>{backend}</button>)}</div>
      </section>
    </div>
  );
}
