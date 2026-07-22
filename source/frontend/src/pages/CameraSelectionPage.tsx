import { Camera as CameraIcon, Radio, RefreshCw, Video } from "lucide-react";
import { Device } from "../types";
import { SelectableCard } from "../components/SelectableCard";

type Props = {
  devices: Device[];
  selectedCameras: string[];
  onToggle: (path: string) => void;
  onRefresh: () => void;
};

export function CameraSelectionPage({ devices, selectedCameras, onToggle, onRefresh }: Props) {
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

      <div className="panel">
        <div className="panel-title">
          <CameraIcon size={18} />
          <h3>Detected Cameras</h3>
        </div>
        {devices.length === 0 ? (
          <div className="empty">No /dev/video* devices were discovered.</div>
        ) : (
          <div className="card-grid vertical-grid">
            {devices.map((device) => (
              <SelectableCard
                key={device.path}
                selected={selectedCameras.includes(device.path)}
                onToggle={() => onToggle(device.path)}
                title={device.path}
                subtitle={`${device.card || "Unknown camera"} · ${device.driver || "unknown driver"}`}
                badges={
                  <>
                    <span className={`capability-chip${device.supports_capture ? " enabled" : ""}`}>
                      <Video size={11} /> Capture
                    </span>
                    <span className={`capability-chip${device.supports_streaming ? " enabled" : ""}`}>
                      <Radio size={11} /> Streaming
                    </span>
                  </>
                }
                layout="vertical"
              />
            ))}
          </div>
        )}
      </div>
    </div>
  );
}
