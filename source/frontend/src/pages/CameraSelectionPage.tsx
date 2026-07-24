import { Camera as CameraIcon, Radio, RefreshCw, Users, Video } from "lucide-react";
import { Device } from "../types";
import { SelectableCard } from "../components/SelectableCard";

type Props = {
  devices: Device[];
  cameraMode: "single" | "multi";
  onCameraModeChange: (mode: "single" | "multi") => void;
  masterPath: string | null;
  onSelectMaster: (path: string) => void;
  slavePaths: string[];
  onToggleSlave: (path: string) => void;
  onRefresh: () => void;
};

function deviceSubtitle(device: Device) {
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
        <div className="segmented-control" aria-label="Camera mode">
          <button className={cameraMode === "single" ? "selected" : ""} onClick={() => onCameraModeChange("single")}>
            Single camera
          </button>
          <button className={cameraMode === "multi" ? "selected" : ""} onClick={() => onCameraModeChange("multi")}>
            Multi-camera
          </button>
        </div>
      </section>

      {devices.length === 0 ? (
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
                return (
                  <SelectableCard
                    key={device.path}
                    selected={slavePaths.includes(device.path)}
                    onToggle={() => onToggleSlave(device.path)}
                    disabled={isMaster}
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
