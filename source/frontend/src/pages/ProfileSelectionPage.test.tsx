import { afterEach, describe, expect, it, vi } from "vitest";
import { render, screen, waitFor } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { ProfileSelectionPage } from "./ProfileSelectionPage";
import { Device, TriggerMode } from "../types";

// Drives the real create-profile flow and inspects the request body that leaves
// the page. Asserting on the constant in the source would prove nothing; the
// point is that the payload POSTed to /api/profiles carries a selector the
// backend can actually resolve.
//
// "implemented" is not an id, category or tag, so select_tests() matches nothing
// and a run built from such a profile executes zero tests
// (see tests/test_selection_test.cpp).

const devices: Device[] = [
  {
    path: "/dev/video1",
    driver: "uvcvideo",
    card: "USB Camera",
    bus_info: "usb-0000:00:14.0-2",
    sysfs_name: "video1",
    supports_capture: true,
    supports_streaming: true,
    formats: [],
    error: ""
  } as unknown as Device,
  {
    path: "/dev/video0",
    driver: "uvcvideo",
    card: "USB Camera",
    bus_info: "usb-0000:00:14.0-1",
    sysfs_name: "video0",
    supports_capture: true,
    supports_streaming: true,
    formats: [],
    error: ""
  } as unknown as Device
];

function renderPage(triggerMode: TriggerMode = "hardware", profileSchemaVersion: number | null = 3) {
  const onProfilesChanged = vi.fn().mockResolvedValue(undefined);
  const onError = vi.fn();
  const { unmount } = render(
    <ProfileSelectionPage
      devices={devices}
      profiles={[]}
      profileSchemaVersion={profileSchemaVersion}
      triggerMode={triggerMode}
      onTriggerModeChange={vi.fn()}
      singleProfileId=""
      onSingleProfileChange={vi.fn()}
      assignments={[]}
      onProfilesChanged={onProfilesChanged}
      onError={onError}
      onSuccess={vi.fn()}
      requestConfirm={vi.fn()}
    />
  );
  return { onError, unmount };
}

/** Captures every fetch call so the POSTed profile can be inspected. */
/**
 * One writable control on one device, so the software-trigger form has something
 * real to select. Shaped like GET /api/control-devices.
 */
const CONTROL_DEVICE = {
  path: "/dev/video0",
  kind: "video",
  driver: "uvcvideo",
  card: "USB Camera",
  bus_info: "usb-0000:00:14.0-1",
  sysfs_name: "video0",
  error: "",
  controls: [
    {
      id: 10094871,
      type: 2,
      name: "Trigger Mode",
      minimum: 0,
      maximum: 1,
      step: 1,
      default_value: 0,
      current_value: 0,
      writable: true,
      supported_for_trigger: true,
      menu_items: []
    }
  ]
};

function stubFetch() {
  const calls: Array<{ url: string; init?: RequestInit }> = [];
  const fetchMock = vi.fn(async (url: RequestInfo | URL, init?: RequestInit) => {
    calls.push({ url: String(url), init });
    // The control-device list has to be real, or the software form has nothing to
    // select and every software test would silently exercise the empty case.
    const body = String(url).includes("/api/control-devices")
      ? { devices: [CONTROL_DEVICE] }
      : { ok: true };
    return new Response(JSON.stringify(body), {
      status: 200,
      headers: { "Content-Type": "application/json" }
    });
  });
  vi.stubGlobal("fetch", fetchMock);
  return calls;
}

function postedProfile(calls: Array<{ url: string; init?: RequestInit }>) {
  const post = calls.find((c) => c.url.includes("/api/profiles") && c.init?.method === "POST");
  expect(post, "no POST to /api/profiles was made").toBeTruthy();
  return JSON.parse(String(post!.init!.body));
}

/** Clears first: the form pre-fills some fields, so typing would append. */
async function setField(user: ReturnType<typeof userEvent.setup>, label: RegExp, value: string) {
  const field = screen.getByLabelText(label);
  await user.clear(field);
  await user.type(field, value);
}

/** Fills the form. `role` is the canonical role the channel drives. */
async function fillAndSave(user: ReturnType<typeof userEvent.setup>, role = "master") {
  await user.click(screen.getByTitle("New profile"));
  await setField(user, /Profile ID/, "anvil");
  await setField(user, /^Name/, "Anvil");
  await setField(user, /Channel ID/, "channel-a");
  const roleSelect = screen.queryByLabelText(/Bind to role/);
  if (roleSelect && role) {
    await user.selectOptions(roleSelect, role);
  }
  await user.click(screen.getByRole("button", { name: /Save profile/ }));
}

afterEach(() => {
  vi.unstubAllGlobals();
});

describe("ProfileSelectionPage create flow", () => {
  it("posts a resolvable test selector, never \"implemented\"", async () => {
    const user = userEvent.setup();
    const calls = stubFetch();
    renderPage("hardware");

    await fillAndSave(user);

    const profile = postedProfile(calls);
    expect(profile.defaults.test_selectors).toEqual(["stable"]);
    expect(profile.defaults.test_selectors).not.toContain("implemented");
    // Guard the whole body, not just the one field: no nested value anywhere may
    // reintroduce the invalid selector.
    expect(JSON.stringify(profile)).not.toContain("implemented");
  });

  it("posts the identity and defaults the user actually chose", async () => {
    const user = userEvent.setup();
    const calls = stubFetch();
    renderPage("hardware");

    await fillAndSave(user);

    const profile = postedProfile(calls);
    expect(profile.id).toBe("anvil");
    expect(profile.name).toBe("Anvil");
    expect(profile.defaults.trigger_mode).toBe("hardware");
    expect(profile.trigger_channels).toHaveLength(1);
    expect(profile.trigger_channels[0].id).toBe("channel-a");
  });

  // The version must come from the backend's own GET /api/profiles response, not
  // from a constant in the frontend. A second mirror of the number has to be found
  // and bumped alongside the backend's, and a missed one breaks profile creation
  // outright -- so the assertion follows the prop rather than hard-coding 3.
  it("stamps new profiles with the schema version the backend reported", async () => {
    for (const version of [3, 4, 17]) {
      const user = userEvent.setup();
      const calls = stubFetch();
      const { unmount } = renderPage("hardware", version);

      await fillAndSave(user);

      expect(postedProfile(calls).schema_version).toBe(version);
      unmount();
      vi.unstubAllGlobals();
    }
  });

  // Rather than guessing: the backend rejects a body whose schema_version it does
  // not recognise, and guessing here is what a frontend constant amounted to.
  it("refuses to create a profile before the backend reports its schema version", async () => {
    const user = userEvent.setup();
    const calls = stubFetch();
    const { onError } = renderPage("hardware", null);

    await fillAndSave(user);

    expect(calls.some((c) => c.init?.method === "POST")).toBe(false);
    expect(onError).toHaveBeenCalledWith(expect.stringContaining("schema version"));
  });

  // Schema v3 dropped `enabled`; the create flow must not resurrect it.
  it("posts no fields the target schema dropped", async () => {
    const user = userEvent.setup();
    const calls = stubFetch();
    renderPage("hardware");

    await fillAndSave(user);

    const profile = postedProfile(calls);
    expect(profile).not.toHaveProperty("enabled");
    expect(JSON.stringify(profile)).not.toContain("enabled");
  });

  // --- v4 role-based routing (plan 2.5) ----------------------------------
  //
  // The profile must not carry a physical camera identity, and the routing it does
  // carry must be an explicit role binding rather than something inferred.
  // The role is chosen, never assumed. An empty selection must stop the POST.
  it("does not post until a run role is chosen", async () => {
    const user = userEvent.setup();
    const calls = stubFetch();
    const { onError } = renderPage("hardware");

    await fillAndSave(user, "");

    expect(calls.some((c) => c.init?.method === "POST")).toBe(false);
    expect(onError).toHaveBeenCalledWith(expect.stringContaining("role"));
  });

  // A slave-only profile binds no master, so it can never start a run and the
  // central validator refuses it. The form must not POST one.
  it("does not post a slave-only profile", async () => {
    const user = userEvent.setup();
    const calls = stubFetch();
    const { onError } = renderPage("hardware");

    await user.click(screen.getByTitle("New profile"));
    await setField(user, /Profile ID/, "slaveonly");
    await setField(user, /^Name/, "SlaveOnly");
    await setField(user, /Channel ID/, "channel-a");
    // Not even on offer: this form creates the first channel, which has to be
    // master. Selecting an absent option leaves the field empty.
    expect(
      [...screen.getByLabelText(/Bind to role/).querySelectorAll("option")].map((o) => o.textContent)
    ).toEqual(["Select a role", "master"]);
    await user.click(screen.getByRole("button", { name: /Save profile/ }));

    expect(calls.some((c) => c.init?.method === "POST")).toBe(false);
    expect(onError).toHaveBeenCalledWith(expect.stringContaining("master"));
  });

  it("posts role_bindings and no physical camera matcher", async () => {
    const user = userEvent.setup();
    const calls = stubFetch();
    renderPage("hardware");

    await fillAndSave(user);

    const profile = postedProfile(calls);
    // v4 dropped both: {driver, card, bus_info} cannot tell this hardware's four
    // video nodes apart, so routing goes by run role instead.
    expect(profile).not.toHaveProperty("camera_match");
    expect(profile).not.toHaveProperty("camera_bindings");
    expect(JSON.stringify(profile)).not.toContain("camera_match");
    expect(JSON.stringify(profile)).not.toContain("camera_bindings");

    // The one channel the user drew is bound to master explicitly.
    expect(profile.role_bindings).toEqual([
      { role: "master", trigger_channel_id: "channel-a" }
    ]);
  });

  it("binds exactly the one role it was told about", async () => {
    const user = userEvent.setup();
    const calls = stubFetch();
    renderPage("hardware");

    await fillAndSave(user);

    // One channel, one binding: inventing a second would route a trigger to a
    // camera the user never mentioned.
    const roles = postedProfile(calls).role_bindings.map((b: { role: string }) => b.role);
    expect(roles).toEqual(["master"]);
  });

  /** Fills the software form. Skips a step when its argument is omitted. */
  async function fillSoftware(
    user: ReturnType<typeof userEvent.setup>,
    opts: { device?: boolean; control?: boolean } = { device: true, control: true }
  ) {
    await user.click(screen.getByTitle("New profile"));
    await setField(user, /Profile ID/, "soft");
    await setField(user, /^Name/, "Soft");
    await setField(user, /Channel ID/, "channel-s");
    await user.selectOptions(screen.getByLabelText(/Bind to role/), "master");
    if (opts.device) {
      await waitFor(() =>
        expect(screen.getByLabelText(/Control device/).querySelectorAll("option").length).toBeGreaterThan(1)
      );
      await user.selectOptions(screen.getByLabelText(/Control device/), CONTROL_DEVICE.path);
    }
    if (opts.control) {
      await user.selectOptions(screen.getByLabelText(/Fire control/), String(CONTROL_DEVICE.controls[0].id));
    }
    await user.click(screen.getByRole("button", { name: /Save profile/ }));
  }

  // A software channel is driven by a control write, so one with no fire control
  // cannot fire anything -- validate_device_profile() refuses it. The form must not
  // POST that. The old version of this test passed only because the fetch stub
  // answered 200 to everything, so an invalid profile looked accepted.
  it("does not post a software profile without a control device", async () => {
    const user = userEvent.setup();
    const calls = stubFetch();
    const { onError } = renderPage("software");

    await fillSoftware(user, { device: false, control: false });

    expect(calls.some((c) => c.init?.method === "POST")).toBe(false);
    expect(onError).toHaveBeenCalledWith(expect.stringContaining("control device"));
  });

  it("does not post a software profile without a fire control", async () => {
    const user = userEvent.setup();
    const calls = stubFetch();
    const { onError } = renderPage("software");

    await fillSoftware(user, { device: true, control: false });

    expect(calls.some((c) => c.init?.method === "POST")).toBe(false);
    // Names the field, so the user is not left guessing which one is missing.
    expect(onError).toHaveBeenCalledWith(expect.stringContaining("fire control"));
  });

  it("posts a software profile that satisfies the backend validator", async () => {
    const user = userEvent.setup();
    const calls = stubFetch();
    renderPage("software");

    await fillSoftware(user);

    const profile = postedProfile(calls);
    // Every rule validate_device_profile() applies to a software channel.
    expect(profile.trigger_channels).toHaveLength(1);
    const channel = profile.trigger_channels[0];
    expect(channel.type).toBe("software");
    expect(channel.fire).toHaveLength(1);
    expect(channel.fire[0].id).toBe(CONTROL_DEVICE.controls[0].id);
    expect(channel.control_device.kind).toBe("capture");
    expect(profile.role_bindings).toEqual([
      { role: "master", trigger_channel_id: "channel-s" }
    ]);
    expect(profile).not.toHaveProperty("camera_match");
  });

  // v5 removed the field from the profile schema: the user does not choose formats,
  // every run writes HTML, JSON and Markdown (plan 2.10). A new profile must not
  // carry the key at all -- also a decision guard (protocol P8).
  it("posts no report_formats at all", async () => {
    const user = userEvent.setup();
    const calls = stubFetch();
    renderPage("hardware");

    await fillAndSave(user);

    const profile = postedProfile(calls);
    expect(profile.defaults).not.toHaveProperty("report_formats");
    expect(JSON.stringify(profile)).not.toContain("report_formats");
  });

  it("hides profile action buttons (New, Import, Export, Delete) in free-run mode", () => {
    renderPage("free-run");

    expect(screen.queryByTitle("New profile")).not.toBeInTheDocument();
    expect(screen.queryByTitle("Import profile")).not.toBeInTheDocument();
    expect(screen.queryByTitle("Export selected profile")).not.toBeInTheDocument();
    expect(screen.queryByTitle("Delete selected profile")).not.toBeInTheDocument();
  });
});
