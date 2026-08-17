import { Camera as CameraIcon, Radio, RefreshCw, Users, Video } from "lucide-react";
import { Device } from "../types";
import { SelectableCard } from "../components/SelectableCard";

type Props = {
  devices: Device[];
  loading?: boolean;
  error?: string | null;
  cameraMode: "single" | "multi";
  onCameraModeChange: (mode: "single" | "multi") => void;
  masterPath: string | null;
  onSelectMaster: (path: string) => void;
  slavePaths: string[];
  onToggleSlave: (path: string) => void;
  onRefresh: () => void;
};

function deviceSubtitle(device: Device) {
  if (!device.supports_capture) {
    return device.error || "Does not support V4L2 video capture capability";
  }
  if (device.error) {
    return `${device.card || "Unknown camera"} · ${device.driver || "unknown driver"} (${device.error})`;
  }
  return `${device.card || "Unknown camera"} · ${device.driver || "unknown driver"}`;
}

function deviceBadges(device: Device) {
  return (
    <>
      <span className={`capability-chip${device.supports_capture ? " enabled" : ""}`}>
        <Video size={11} /> Capture
      </span>
      <span className={`capability-chip${device.supports_streaming ? " enabled" : ""}`}>
        <Radio size={11} /> Streaming
      </span>
    </>
  );
}

export function CameraSelectionPage({
  devices,
  loading = false,
  error = null,
  cameraMode,
  onCameraModeChange,
  masterPath,
  onSelectMaster,
  slavePaths,
  onToggleSlave,
  onRefresh
}: Props) {
  return (
    <div className="page">
      <header className="topbar">
        <div>
          <p className="eyebrow">Configure</p>
          <h2>Camera Selection</h2>
        </div>
        <button className="icon-button" onClick={onRefresh} title="Refresh devices and metadata">
          <RefreshCw size={18} />
        </button>
      </header>

      <section className="routing-toolbar">
        <div className="segmented-control" role="group" aria-label="Camera mode">
          <button
            className={cameraMode === "single" ? "selected" : ""}
            onClick={() => onCameraModeChange("single")}
            aria-pressed={cameraMode === "single"}
          >
            Single camera
          </button>
          <button
            className={cameraMode === "multi" ? "selected" : ""}
            onClick={() => onCameraModeChange("multi")}
            aria-pressed={cameraMode === "multi"}
          >
            Multi-camera
          </button>
        </div>
      </section>

      {loading ? (
        <div className="panel">
          <div className="empty">Loading discovered video devices...</div>
        </div>
      ) : error ? (
        <div className="panel">
          <div className="empty error-text">{error}</div>
        </div>
      ) : devices.length === 0 ? (
        <div className="panel">
          <div className="empty">No /dev/video* devices were discovered.</div>
        </div>
      ) : cameraMode === "single" ? (
        <div className="panel">
          <div className="panel-title">
            <CameraIcon size={18} />
            <h3>Camera Under Test</h3>
          </div>
          <div className="card-grid vertical-grid">
            {devices.map((device) => (
              <SelectableCard
                key={device.path}
                selected={masterPath === device.path}
                onToggle={() => onSelectMaster(device.path)}
                disabled={!device.supports_capture}
                title={device.path}
                subtitle={deviceSubtitle(device)}
                badges={deviceBadges(device)}
                layout="vertical"
              />
            ))}
          </div>
        </div>
      ) : (
        <div className="master-slave-grid">
          <div className="panel">
            <div className="panel-title">
              <CameraIcon size={18} />
              <h3>Master (Under Test)</h3>
            </div>
            <p className="panel-hint">The full diagnostic suite runs against this camera.</p>
            <div className="card-grid vertical-grid">
              {devices.map((device) => (
                <SelectableCard
                  key={device.path}
                  selected={masterPath === device.path}
                  onToggle={() => onSelectMaster(device.path)}
                  disabled={!device.supports_capture}
                  title={device.path}
                  subtitle={deviceSubtitle(device)}
                  badges={deviceBadges(device)}
                  layout="vertical"
                />
              ))}
            </div>
          </div>

          <div className="panel">
            <div className="panel-title">
              <Users size={18} />
              <h3>Slaves (t25 Multi-Camera Only)</h3>
            </div>
            <p className="panel-hint">
              Extra cameras that watch the master&apos;s trigger pulse for cross-device jitter measurement.
            </p>
            <div className="card-grid vertical-grid">
              {devices.map((device) => {
                const isMaster = device.path === masterPath;
                const isDisabled = isMaster || !device.supports_capture;
                return (
                  <SelectableCard
                    key={device.path}
                    selected={slavePaths.includes(device.path)}
                    onToggle={() => onToggleSlave(device.path)}
                    disabled={isDisabled}
                    title={device.path}
                    subtitle={isMaster ? "Already selected as master" : deviceSubtitle(device)}
                    badges={!isMaster && deviceBadges(device)}
                    layout="vertical"
                  />
                );
              })}
            </div>
          </div>
        </div>
      )}
    </div>
  );
}
