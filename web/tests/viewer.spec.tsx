import { expect, test, type Page } from "@playwright/test";

interface Statistics {
  geometryUploads: number;
  scalarUploads: number;
  renders: number;
  backend: string;
}
async function stats(page: Page): Promise<Statistics> {
  const summary = page.getByText("Diagnostics", { exact: true });
  if (!(await page.getByRole("button", { name: "Statistics", exact: true }).isVisible()))
    await summary.click();
  await page.getByRole("button", { name: "Statistics", exact: true }).click();
  const text = await page.getByTestId("stats").innerText();
  const result: unknown = JSON.parse(text);
  if (
    !result ||
    typeof result !== "object" ||
    !("geometryUploads" in result) ||
    typeof result.geometryUploads !== "number" ||
    !("scalarUploads" in result) ||
    typeof result.scalarUploads !== "number" ||
    !("renders" in result) ||
    typeof result.renders !== "number" ||
    !("backend" in result) ||
    typeof result.backend !== "string"
  )
    throw new Error("Invalid viewer statistics");
  return {
    geometryUploads: result.geometryUploads,
    scalarUploads: result.scalarUploads,
    renders: result.renders,
    backend: result.backend,
  };
}

for (const backend of ["webgl", "software"]) {
  test(`${backend}: scalar clipping, spatial clipping, camera, frames and teardown`, async ({
    page,
    browserName,
  }) => {
    const errors: string[] = [];
    page.on("pageerror", (error) => errors.push(error.message));
    await page.goto(backend === "software" ? "/?backend=software" : "/");
    await expect(page.getByTestId("backend")).toHaveText(backend);
    await expect(page.getByRole("alert")).toHaveCount(0);
    const surface = page.locator("canvas");
    await expect(surface).toBeVisible();
    const initial = await surface.screenshot();
    if (backend === "webgl" && browserName === "chromium")
      await page.screenshot({ path: "test-results/demo.png", fullPage: true });
    const before = await stats(page);
    const initialBox = await surface.boundingBox();
    if (!initialBox) throw new Error("Missing canvas bounds");
    await page.mouse.move(
      initialBox.x + initialBox.width / 2,
      initialBox.y + initialBox.height / 2,
    );
    await expect(page.getByTestId("hover")).toHaveText("Tag A");
    await page.mouse.click(
      initialBox.x + initialBox.width / 2,
      initialBox.y + initialBox.height / 2,
    );
    await expect(page.getByTestId("selected")).toHaveText("Tag A");

    await page.getByRole("button", { name: "Next frame" }).click();
    await expect(page.getByTestId("frame")).toHaveText("1");
    await expect.poll(async () => (await stats(page)).renders).toBeGreaterThan(before.renders);
    const after = await stats(page);

    expect(after.geometryUploads).toBe(before.geometryUploads);
    expect(after.scalarUploads).toBe(before.scalarUploads + 1);
    expect(await surface.screenshot()).not.toEqual(initial);

    await page.getByLabel("Threshold 1", { exact: true }).dblclick();
    await page.getByRole("spinbutton", { name: "Threshold 1" }).fill("-30");
    await page.getByRole("spinbutton", { name: "Threshold 1" }).press("Tab");
    await expect.poll(async () => (await stats(page)).renders).toBeGreaterThan(after.renders);
    const recolored = await stats(page);

    expect(recolored.geometryUploads).toBe(after.geometryUploads);
    expect(recolored.scalarUploads).toBe(after.scalarUploads);

    const lowerHandle = page.getByRole("slider", { name: "Clip minimum", exact: true });
    const handleBox = await lowerHandle.boundingBox();
    if (!handleBox) throw new Error("Missing scalar handle");
    await page.mouse.move(handleBox.x + handleBox.width / 2, handleBox.y + handleBox.height / 2);
    await page.mouse.down();
    await page.mouse.move(handleBox.x + handleBox.width / 2, handleBox.y - 30, { steps: 3 });
    await page.mouse.up();

    expect(Number(await lowerHandle.getAttribute("aria-valuenow"))).toBeGreaterThan(-30);
    await lowerHandle.press("End");
    await expect(lowerHandle).toHaveAttribute("aria-valuenow", "40");
    await lowerHandle.press("Home");
    await expect(lowerHandle).toHaveAttribute("aria-valuenow", "-80");

    const full = await surface.screenshot();
    await page.getByRole("button", { name: "Clip X" }).click();
    await expect.poll(async () => (await stats(page)).renders).toBeGreaterThan(recolored.renders);

    expect(await surface.screenshot()).not.toEqual(full);

    await page.getByRole("combobox", { name: "Scalar" }).selectOption("Regions");
    await expect(page.getByLabel("Scalar bar")).toHaveText("1.002.00");
    await expect(page.getByRole("alert")).toHaveCount(0);
    await page.getByRole("combobox", { name: "Scalar" }).selectOption("None");
    await page.getByRole("button", { name: "Wireframe" }).click();
    const box = await surface.boundingBox();
    if (!box) throw new Error("Missing canvas bounds");
    const resting = await surface.screenshot();
    await page.mouse.move(box.x + box.width * 0.6, box.y + box.height * 0.6);
    await page.mouse.down();
    await page.mouse.move(box.x + box.width * 0.7, box.y + box.height * 0.5, { steps: 8 });
    await page.mouse.up();

    expect(await surface.screenshot()).not.toEqual(resting);

    const rotated = await surface.screenshot();
    await page.mouse.wheel(0, -120);

    expect(await surface.screenshot()).not.toEqual(rotated);

    const zoomed = await surface.screenshot();
    await page.mouse.down({ button: "right" });
    await page.mouse.move(box.x + box.width * 0.8, box.y + box.height * 0.55, { steps: 4 });
    await page.mouse.up({ button: "right" });

    expect(await surface.screenshot()).not.toEqual(zoomed);

    await page.getByRole("button", { name: "Reset camera" }).click();
    expect(await surface.screenshot()).toEqual(resting);
    const idle = await stats(page);
    await page.waitForTimeout(200);

    expect((await stats(page)).renders).toBe(idle.renders);

    await page.getByRole("button", { name: "Unmount", exact: true }).click();
    await expect(surface).toHaveCount(0);
    await page.getByRole("button", { name: "Mount", exact: true }).click();
    await expect(surface).toBeVisible();
    await expect.poll(async () => (await stats(page)).geometryUploads).toBe(1);
    expect(errors).toEqual([]);
  });
}

test("context loss recreates the viewer with software rendering", async ({ page }) => {
  await page.goto("/");
  await expect(page.getByTestId("backend")).toHaveText("webgl");
  await expect.poll(async () => (await stats(page)).renders).toBeGreaterThan(0);

  await page.evaluate(() => {
    const canvas = document.querySelector("canvas");
    const gl = canvas?.getContext("webgl2");
    const extension = gl?.getExtension("WEBGL_lose_context");
    if (!extension) throw new Error("Context loss extension unavailable");
    extension.loseContext();
  });

  await expect(page.getByTestId("backend")).toHaveText("software");
  await expect(page.getByRole("alert")).toHaveCount(0);
  await expect.poll(async () => (await stats(page)).geometryUploads).toBe(1);
});

test("large geometry retains buffers across simulation frames", async ({ page }) => {
  await page.goto("/?large");
  await expect(page.getByTestId("backend")).toHaveText("webgl");
  await expect.poll(async () => (await stats(page)).renders).toBeGreaterThan(0);
  const before = await stats(page);

  const start = Date.now();
  for (let i = 0; i < 5; ++i) {
    await page.getByRole("button", { name: "Next frame" }).click();
    await expect(page.getByTestId("frame")).toHaveText(String(i + 1));
  }
  await expect.poll(async () => (await stats(page)).scalarUploads).toBe(before.scalarUploads + 5);

  expect((await stats(page)).geometryUploads).toBe(before.geometryUploads);
  expect(Date.now() - start).toBeLessThan(15_000);
  await expect(page.getByRole("alert")).toHaveCount(0);
});
