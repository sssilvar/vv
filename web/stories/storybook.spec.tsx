import { expect, test } from "@playwright/test";

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
