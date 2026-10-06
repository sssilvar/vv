import { expect, test } from "@playwright/test";

test.use({ launchOptions: { args: ["--disable-webgl"] } });

test("automatic software fallback with GPU APIs disabled", async ({ page, browserName }) => {
  test.skip(browserName !== "chromium", "This test disables WebGL through a Chromium launch flag");
  await page.goto("/");

  await expect(page.getByTestId("backend")).toHaveText("software");
  await expect(page.getByRole("alert")).toHaveCount(0);
  await expect(page.locator("canvas")).toBeVisible();
  await page.getByText("Diagnostics", { exact: true }).click();
  await page.getByRole("button", { name: "Statistics", exact: true }).click();
  await expect(page.getByTestId("stats")).toContainText('"geometryUploads":1');
});
