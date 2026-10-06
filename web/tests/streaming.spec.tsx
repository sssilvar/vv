import { expect, test } from "@playwright/test";
import type {} from "./harness";

for (const backend of ["webgl", "software"]) {
  test(`${backend}: wireframe resource use stays bounded and idle rendering stops`, async ({
    page,
    browserName,
  }, info) => {
    test.skip(browserName !== "chromium", "Resource benchmark runs once per backend");
    test.setTimeout(120_000);
    await page.goto(`/tests/harness.html?backend=${backend}`);
    await expect
      .poll(() => page.evaluate(() => window.viewerTest.statistics()?.renders ?? 0))
      .toBeGreaterThan(0);
    const solid = await page.evaluate(() => window.viewerTest.benchmark(45));
    await page.evaluate(() => window.viewerTest.wireframe(true));
    const wireframe = await page.evaluate(() => window.viewerTest.benchmark(45));
    const warm = await page.evaluate(() => window.viewerTest.statistics());
    if (!warm) throw new Error("Missing statistics");

    for (let i = 0; i < 8; ++i) {
      await page.evaluate(
        async (enabled) => {
          window.viewerTest.wireframe(enabled);
          await new Promise<void>((resolve) =>
            requestAnimationFrame(() => requestAnimationFrame(() => resolve())),
          );
        },
        i % 2 === 0,
      );
    }
    const settled = await page.evaluate(() => window.viewerTest.statistics());
    await page.waitForTimeout(300);
    expect(await page.evaluate(() => window.viewerTest.statistics())).toEqual(settled);
    expect(settled?.geometryUploads).toBe(warm.geometryUploads);
    expect(settled?.wasmHeapBytes).toBe(warm.wasmHeapBytes);
    expect(solid.heapGrowthBytes).toBe(0);
    expect(wireframe.heapGrowthBytes).toBe(0);
    expect(warm.stagingBytes).toBeLessThan(1024 * 1024);
    const report = {
      backend,
      solid,
      wireframe,
      heapBytes: warm.wasmHeapBytes,
      stagingBytes: warm.stagingBytes,
    };
    console.log("Rendering resource report", report);
    await info.attach("rendering-resource-use", {
      body: JSON.stringify(report, null, 2),
      contentType: "application/json",
    });
  });
}

test("streaming coalesces frames, bounds memory, and preserves the camera after context loss", async ({
  page,
  browserName,
}, info) => {
  if (browserName === "firefox") test.setTimeout(120_000);
  const errors: string[] = [];
  page.on("pageerror", (error) => errors.push(error.message));
  await page.goto("/tests/harness.html");
  await expect
    .poll(() => page.evaluate(() => window.viewerTest.statistics()?.renders ?? 0))
    .toBeGreaterThan(0);
  const renderer = await page.evaluate(() => {
    const gl = document.querySelector("canvas")?.getContext("webgl2");
    const debug = gl?.getExtension("WEBGL_debug_renderer_info");
    const value: unknown =
      gl && debug ? gl.getParameter(debug.UNMASKED_RENDERER_WEBGL) : "unavailable";
    return typeof value === "string" ? value : "unavailable";
  });
  console.log(info.project.name, "renderer", renderer);
  const initial = await page.evaluate(() => window.viewerTest.statistics());
  if (!initial) throw new Error("missing statistics");

  await page.evaluate(() => {
    window.viewerTest.pose();
    window.viewerTest.burst();
  });
  await expect
    .poll(() => page.evaluate(() => window.viewerTest.statistics()?.scalarUploads))
    .toBe(initial.scalarUploads + 1);
  const camera = await page.evaluate(() => window.viewerTest.camera());
  const report = await page.evaluate(
    (frames) => window.viewerTest.benchmark(frames),
    browserName === "firefox" ? 32 : 90,
  );
  await info.attach("streaming timings (CPU, excludes GPU completion)", {
    body: JSON.stringify(report, null, 2),
    contentType: "application/json",
  });
  console.log(info.project.name, "streaming", report);

  expect(report.heapGrowthBytes).toBe(0);
  expect(await page.evaluate(() => window.viewerTest.statistics()?.geometryUploads)).toBe(
    initial.geometryUploads,
  );
  expect(await page.evaluate(() => window.viewerTest.statistics()?.stagingBytes)).toBeLessThan(
    1024 * 1024,
  );

  await page.evaluate(() =>
    document
      .querySelector("canvas")
      ?.getContext("webgl2")
      ?.getExtension("WEBGL_lose_context")
      ?.loseContext(),
  );
  await expect(page.locator("canvas")).toHaveAttribute("data-backend", "software");
  await expect
    .poll(() => page.evaluate(() => window.viewerTest.statistics()?.renders ?? 0))
    .toBeGreaterThan(0);
  const restored = await page.evaluate(() => window.viewerTest.camera());
  if (!camera || !restored) throw new Error("missing camera state");
  for (let i = 0; i < 3; ++i) {
    expect(restored.position[i]).toBeCloseTo(camera.position[i], 10);
    expect(restored.focalPoint[i]).toBeCloseTo(camera.focalPoint[i], 10);
    expect(restored.viewUp[i]).toBeCloseTo(camera.viewUp[i], 10);
  }
  expect(errors).toEqual([]);
  await expect(page.getByRole("alert")).toHaveCount(0);
});

test("compare direct scalar transfers with the previous staging-copy path", async ({
  page,
  browserName,
}, info) => {
  test.skip(browserName !== "chromium", "Transfer benchmark needs only one JS engine");
  await page.goto("/tests/harness.html");
  await expect
    .poll(() => page.evaluate(() => window.viewerTest.statistics()?.renders ?? 0))
    .toBeGreaterThan(0);

  const report = await page.evaluate(() => window.viewerTest.transferBenchmark());

  await info.attach("scalar transfer comparison", {
    body: JSON.stringify(report, null, 2),
    contentType: "application/json",
  });
  console.log("Scalar transfer comparison", report);
  expect(report.bytesPerFrame).toBeGreaterThan(500_000);
});

test("two canvases keep independent cameras and survive peer disposal", async ({ page }) => {
  await page.goto("/tests/harness.html?multi");
  await expect(page.locator("canvas")).toHaveCount(2);
  await expect
    .poll(() => page.evaluate(() => window.viewerTest.statistics()?.renders ?? 0))
    .toBeGreaterThan(0);
  await expect(page.locator("canvas").nth(1)).toHaveAttribute("data-backend", "webgl");
  const peer = await page.locator("canvas").nth(1).screenshot();
  const before = await page.locator("canvas").first().screenshot();

  await page.evaluate(() => {
    window.viewerTest.pose();
    window.viewerTest.burst();
  });
  await expect
    .poll(() => page.evaluate(() => window.viewerTest.statistics()?.scalarUploads ?? 0))
    .toBeGreaterThan(1);

  expect(await page.locator("canvas").nth(1).screenshot()).toEqual(peer);
  expect(await page.locator("canvas").first().screenshot()).not.toEqual(before);
  await page.evaluate(() => window.viewerTest.removePeer());
  await expect(page.locator("canvas")).toHaveCount(1);
  const renders = await page.evaluate(() => window.viewerTest.statistics()?.renders ?? 0);
  await page.evaluate(() => window.viewerTest.burst());
  await expect
    .poll(() => page.evaluate(() => window.viewerTest.statistics()?.renders ?? 0))
    .toBeGreaterThan(renders);
  await expect(page.getByRole("alert")).toHaveCount(0);
});

test("vertical drag follows the pointer and reset restores the complete pose", async ({ page }) => {
  await page.goto("/tests/harness.html");
  await expect
    .poll(() => page.evaluate(() => window.viewerTest.statistics()?.renders ?? 0))
    .toBeGreaterThan(0);
  const home = await page.evaluate(() => window.viewerTest.camera());
  const initialPoint = await page.evaluate(() => window.viewerTest.frontPoint());
  const box = await page.locator("canvas").boundingBox();
  if (!box || !initialPoint) throw new Error("missing viewer");

  await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
  await page.mouse.down();
  await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2 + 60, { steps: 4 });
  await page.mouse.up();

  await expect
    .poll(() => page.evaluate(() => window.viewerTest.frontPoint()?.[1] ?? 0))
    .toBeGreaterThan(initialPoint[1]);

  await page.evaluate(() => window.viewerTest.reset());

  expect(await page.evaluate(() => window.viewerTest.camera())).toEqual(home);
});
