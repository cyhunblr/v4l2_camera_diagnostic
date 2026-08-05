import { render, screen, fireEvent } from "@testing-library/react";
import { describe, expect, it, vi } from "vitest";
import { CameraSelectionPage } from "./CameraSelectionPage";
import { Device } from "../types";

const mockDevices: Device[] = [
  {
    path: "/dev/video0",
    driver: "uvcvideo",
    card: "Integrated Camera",
    bus_info: "usb-0000:00:1.4-1",
    supports_capture: true,
    supports_streaming: true,
    readable: true,
    error: ""
  },
  {
    path: "/dev/video1",
    driver: "uvcvideo",
    card: "Metadata Node",
    bus_info: "usb-0000:00:1.4-1",
    supports_capture: false,
    supports_streaming: false,
    readable: true,
    error: "Metadata node cannot capture frames"
  }
];

describe("CameraSelectionPage (Faz 4.2)", () => {
  it("disables devices that do not support capture", () => {
    const onSelectMaster = vi.fn();
    render(
      <CameraSelectionPage
        devices={mockDevices}
        cameraMode="single"
        onCameraModeChange={vi.fn()}
        masterPath={null}
        onSelectMaster={onSelectMaster}
        slavePaths={[]}
        onToggleSlave={vi.fn()}
        onRefresh={vi.fn()}
      />
    );

    const video0Card = screen.getByRole("button", { name: /\/dev\/video0/i });
    const video1Card = screen.getByRole("button", { name: /\/dev\/video1/i });

    expect(video0Card).not.toHaveAttribute("aria-disabled", "true");
    expect(video1Card).toHaveAttribute("aria-disabled", "true");

    // Clicking a disabled non-capture card does not trigger selection
    fireEvent.click(video1Card);
    expect(onSelectMaster).not.toHaveBeenCalled();
  });

  it("displays device error or non-capture reason in card subtitle", () => {
    render(
      <CameraSelectionPage
        devices={mockDevices}
        cameraMode="single"
        onCameraModeChange={vi.fn()}
        masterPath={null}
        onSelectMaster={vi.fn()}
        slavePaths={[]}
        onToggleSlave={vi.fn()}
        onRefresh={vi.fn()}
      />
    );

    expect(screen.getByText("Metadata node cannot capture frames")).toBeInTheDocument();
  });

  it("renders loading, error, and empty states distinctly", () => {
    const { rerender } = render(
      <CameraSelectionPage
        devices={[]}
        loading={true}
        cameraMode="single"
        onCameraModeChange={vi.fn()}
        masterPath={null}
        onSelectMaster={vi.fn()}
        slavePaths={[]}
        onToggleSlave={vi.fn()}
        onRefresh={vi.fn()}
      />
    );
    expect(screen.getByText("Loading discovered video devices...")).toBeInTheDocument();

    rerender(
      <CameraSelectionPage
        devices={[]}
        loading={false}
        error="Server connection refused"
        cameraMode="single"
        onCameraModeChange={vi.fn()}
        masterPath={null}
        onSelectMaster={vi.fn()}
        slavePaths={[]}
        onToggleSlave={vi.fn()}
        onRefresh={vi.fn()}
      />
    );
    expect(screen.getByText("Server connection refused")).toBeInTheDocument();

    rerender(
      <CameraSelectionPage
        devices={[]}
        loading={false}
        error={null}
        cameraMode="single"
        onCameraModeChange={vi.fn()}
        masterPath={null}
        onSelectMaster={vi.fn()}
        slavePaths={[]}
        onToggleSlave={vi.fn()}
        onRefresh={vi.fn()}
      />
    );
    expect(screen.getByText("No /dev/video* devices were discovered.")).toBeInTheDocument();
  });

  it("provides ARIA group and aria-pressed attributes for camera mode toolbar", () => {
    render(
      <CameraSelectionPage
        devices={mockDevices}
        cameraMode="single"
        onCameraModeChange={vi.fn()}
        masterPath={null}
        onSelectMaster={vi.fn()}
        slavePaths={[]}
        onToggleSlave={vi.fn()}
        onRefresh={vi.fn()}
      />
    );

    const group = screen.getByRole("group", { name: "Camera mode" });
    expect(group).toBeInTheDocument();

    const singleBtn = screen.getByRole("button", { name: "Single camera" });
    const multiBtn = screen.getByRole("button", { name: "Multi-camera" });

    expect(singleBtn).toHaveAttribute("aria-pressed", "true");
    expect(multiBtn).toHaveAttribute("aria-pressed", "false");
  });
});
