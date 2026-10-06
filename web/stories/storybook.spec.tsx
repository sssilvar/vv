import { expect, test, type Page } from "@playwright/test";

const viewerStories = [
  "plain",
  "point-field",
  "cell-field",
  "cyclic",
  "categorical",
  "missing-values",
  "saturation",
  "spatial-clipping",
  "wireframe",
  "point-cloud",
  "transparent",
  "software",
  "annotations",
  "large-mesh",
  "streaming",
];
for (const story of viewerStories) {
  test(`Viewer: ${story}`, async ({ page }) => {
    const errors: string[] = [];
    page.on("pageerror", (error) => errors.push(error.message));
    await page.goto(`/iframe.html?id=viewer--${story}&viewMode=story`);
    await expect(page.locator("canvas")).toHaveAttribute(
      "data-backend",
      story === "software" ? "software" : "webgl",
    );
    await expect(page.getByRole("alert")).toHaveCount(0);
    expect(errors).toEqual([]);
  });
}
for (const story of ["empty", "invalid-geometry"]) {
  test(`Invalid input: ${story}`, async ({ page }) => {
    await page.goto(`/iframe.html?id=viewer--${story}&viewMode=story`);
    await expect(page.getByRole("alert")).toBeVisible();
  });
}
test("Scalar bar supports keyboard editing and constant ranges", async ({ page }) => {
  await page.goto("/iframe.html?id=scalarbar--interactive&viewMode=story");
  const minimum = page.getByRole("slider", { name: "Clip minimum" });
  await minimum.focus();
  await page.keyboard.press("ArrowUp");
  await expect(minimum).toHaveAttribute("aria-valuenow", "-78.8");
  await page.goto("/iframe.html?id=scalarbar--constant&viewMode=story");
  await expect(page.getByRole("slider", { name: "Clip minimum" })).toBeDisabled();
});
test("Streaming and reset remain usable at narrow widths", async ({ page }) => {
  await page.setViewportSize({ width: 390, height: 844 });
  await page.goto("/iframe.html?id=viewer--streaming&viewMode=story");
  await expect(page.locator("canvas")).toHaveAttribute("data-backend", "webgl");
  await page.getByRole("button", { name: "Next frame" }).click();
  await expect(page.getByLabel("Frame", { exact: true })).toHaveText("1");
  await page.getByRole("button", { name: "Reset camera" }).click();
  expect(await page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth)).toBe(
    true,
  );
  await page.screenshot({ path: "test-results/storybook-narrow.png" });
  await page.setViewportSize({ width: 1280, height: 850 });
  await page.screenshot({ path: "test-results/storybook-wide.png" });
});

test("Streaming updates the surface, pauses and resumes", async ({ page }) => {
  await page.goto("/iframe.html?id=viewer--streaming&viewMode=story");
  await expect(page.getByLabel("Renderer", { exact: true })).toHaveText("webgl");
  const canvas = page.locator("canvas");
  const initial = await canvas.screenshot();
  const frame = page.getByLabel("Frame", { exact: true });

  await page.getByRole("button", { name: "Play", exact: true }).click();
  await expect(page.getByRole("button", { name: "Next frame" })).toBeDisabled();
  await expect.poll(async () => Number(await frame.textContent())).toBeGreaterThan(5);
  await page.getByRole("button", { name: "Pause", exact: true }).click();
  expect(await canvas.screenshot()).not.toEqual(initial);
  const paused = Number(await frame.textContent());
  await page.waitForTimeout(250);
  await expect(frame).toHaveText(String(paused));

  await page.getByRole("button", { name: "Next frame" }).click();
  await expect(frame).toHaveText(String(paused + 1));
  await page.getByRole("button", { name: "Play", exact: true }).click();
  await expect.poll(async () => Number(await frame.textContent())).toBeGreaterThan(paused + 1);
  await page.getByRole("button", { name: "Pause", exact: true }).click();
});

async function brightnessRatio(page: Page): Promise<number> {
  const screenshot = await page.locator("canvas").screenshot();
  return page.evaluate(async (png) => {
    const image = new Image();
    image.src = `data:image/png;base64,${png}`;
    await image.decode();
    const canvas = document.createElement("canvas");
    canvas.width = image.width;
    canvas.height = image.height;
    const context = canvas.getContext("2d");
    if (!context) throw new Error("Missing image context");
    context.drawImage(image, 0, 0);
    const { data } = context.getImageData(0, 0, canvas.width, canvas.height);
    const brightness = [0, 0];
    for (let y = 0; y < canvas.height; ++y) {
      for (let x = 0; x < canvas.width; ++x) {
        const i = (y * canvas.width + x) * 4;
        brightness[Number(x >= canvas.width / 2)] += data[i] + data[i + 1] + data[i + 2];
      }
    }
    return brightness[1] / brightness[0];
  }, screenshot.toString("base64"));
}

for (const backend of ["webgl", "software"]) {
  for (const opacity of [1, 0.4]) {
    test(`${backend}: darker scalar back faces at opacity ${opacity}`, async ({ page }) => {
      await page.goto(
        `/iframe.html?id=viewer--shell-sides&viewMode=story&args=backend:${backend};opacity:${opacity}`,
      );
      await expect(page.getByLabel("Renderer", { exact: true })).toHaveText(backend);
      const ratio = await brightnessRatio(page);
      expect(ratio).toBeGreaterThan(0.35);
      expect(ratio).toBeLessThan(0.55);
    });
  }
}

for (const backend of ["webgl", "software"]) {
  test(`${backend}: offset lighting reveals surface curvature`, async ({ page }) => {
    await page.goto(`/iframe.html?id=viewer--plain&viewMode=story&args=backend:${backend}`);
    await expect(page.getByLabel("Renderer", { exact: true })).toHaveText(backend);
    // A symmetric surface under a headlight would have equally bright halves.
    expect(await brightnessRatio(page)).toBeLessThan(0.85);
    const canvas = page.locator("canvas");
    const before = await canvas.screenshot();
    const box = await canvas.boundingBox();
    if (!box) throw new Error("Missing canvas bounds");
    await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
    await page.mouse.down();
    await page.mouse.move(box.x + box.width * 0.65, box.y + box.height * 0.6, { steps: 8 });
    await page.mouse.up();
    expect(await canvas.screenshot()).not.toEqual(before);
    await page.getByRole("button", { name: "Reset camera" }).click();
    await expect.poll(() => brightnessRatio(page)).toBeLessThan(0.85);
    await canvas.screenshot({ path: `../.cache/visual-review/lighting-${backend}.png` });
  });
  test(`${backend}: wireframe has equally bright front and back edges`, async ({ page }) => {
    await page.goto(
      `/iframe.html?id=viewer--shell-sides&viewMode=story&args=backend:${backend};wireframe:true`,
    );
    await expect(page.getByLabel("Renderer", { exact: true })).toHaveText(backend);
    const ratio = await brightnessRatio(page);
    expect(ratio).toBeGreaterThan(0.9);
    expect(ratio).toBeLessThan(1.1);
  });
}
