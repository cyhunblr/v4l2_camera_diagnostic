import { expect, Page, test } from "@playwright/test";

const devices = [
  { path: "/dev/video0", driver: "uvcvideo", card: "USB Camera", bus_info: "usb-0000:00:14.0-2", supports_capture: true, supports_streaming: true, error: "" },
  { path: "/dev/video2", driver: "imx219", card: "Camera Sensor A", bus_info: "platform:csi-0", supports_capture: true, supports_streaming: true, error: "" },
  { path: "/dev/video4", driver: "imx219", card: "Camera Sensor B", bus_info: "platform:csi-1", supports_capture: true, supports_streaming: true, error: "" }
];

const profile = {
  schema_version: 2,
  id: "lab-routing",
  name: "Lab routing",
  description: "Playwright fixture",
  enabled: true,
  camera_match: { driver: "", card: "", bus_info: "" },
  defaults: { trigger_mode: "hardware", memory_backends: ["mmap"], test_selectors: ["implemented"], report_formats: ["json"] },
  trigger_channels: [
    { id: "line-0", name: "", description: "", type: "hardware", gpio: { chip_id: 0, line_number: 12, description: "" } },
    { id: "line-1", name: "Frame pulse", description: "", type: "hardware", gpio: { chip_id: 1, line_number: 7, description: "" } }
  ],
  camera_bindings: []
};

async function mockApi(page: Page) {
  await page.route("**/api/**", async (route) => {
    const path = new URL(route.request().url()).pathname;
    const body = path === "/api/devices" ? { devices }
      : path === "/api/profiles" ? { profiles: [profile] }
      : path === "/api/tests" ? { tests: [] }
      : path === "/api/runs" ? { runs: [] }
      : { devices: [], runs: [] };
    await route.fulfill({ status: 200, contentType: "application/json", body: JSON.stringify(body) });
  });
}

async function openRouting(page: Page) {
  await mockApi(page);
  await page.goto("/");
  const mobileNavigation = page.getByLabel("Navigate");
  if (await mobileNavigation.isVisible()) {
    await mobileNavigation.selectOption("cameras");
  } else {
    await page.getByRole("button", { name: "Cameras", exact: true }).click();
  }
  for (const device of devices) {
    await page.getByText(device.path, { exact: true }).click();
  }
  if (await mobileNavigation.isVisible()) {
    await mobileNavigation.selectOption("profiles");
  } else {
    await page.getByRole("button", { name: "Profiles", exact: true }).click();
  }
  await page.getByRole("button", { name: "Hardware", exact: true }).click();
  await expect(page.getByRole("heading", { name: "Trigger Routing" })).toBeVisible();
  await expect(page.locator(".camera-routing-node")).toHaveCount(3);
  await expect(page.locator(".channel-routing-node")).toHaveCount(2);
  await expect(page.getByText("gpiochip0 · line 12", { exact: true }).first()).toBeVisible();
  await expect(page.getByText("Frame pulse", { exact: true })).toBeVisible();
  const sources = page.locator(".camera-routing-node .react-flow__handle");
  const targets = page.locator(".channel-routing-node .react-flow__handle");
  await sources.nth(0).click();
  await targets.nth(0).click();
  await sources.nth(1).click();
  await targets.nth(0).click();
  await sources.nth(2).click();
  await targets.nth(1).click();
  await expect(page.locator(".react-flow__edge")).toHaveCount(3);
  await expect(page.getByText("3 cameras routed · 0 unassigned", { exact: true })).toBeVisible();
}

test("trigger routing is framed on desktop", async ({ page }) => {
  await page.setViewportSize({ width: 1440, height: 1000 });
  await openRouting(page);
  await expect(page.locator(".routing-canvas-section")).toBeInViewport();
  await page.screenshot({ path: "test-results/trigger-routing-desktop.png", fullPage: true });
});

test("trigger routing remains usable on mobile", async ({ page }) => {
  await page.setViewportSize({ width: 390, height: 844 });
  await openRouting(page);
  const documentWidth = await page.evaluate(() => document.documentElement.scrollWidth);
  expect(documentWidth).toBeLessThanOrEqual(390);
  await expect(page.locator(".routing-canvas-section")).toBeVisible();
  await page.screenshot({ path: "test-results/trigger-routing-mobile.png", fullPage: true });
});
