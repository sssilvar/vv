import { defineConfig } from "@playwright/test";

export default defineConfig({
  testDir: "tests",
  testMatch: "**/*.spec.tsx",
  workers: 1,
  retries: 0,
  timeout: 60_000,
  use: {
    baseURL: "http://127.0.0.1:5186",
    viewport: { width: 1280, height: 850 },
    screenshot: "only-on-failure",
    trace: "retain-on-failure",
  },
  projects: [
    ...(process.env.VV_GPU_BENCHMARK === "1"
      ? [
          {
            name: "chromium-gpu",
            testMatch: "**/streaming.spec.tsx",
            use: {
              browserName: "chromium" as const,
              launchOptions: { args: ["--enable-gpu", "--use-gl=angle"] },
            },
          },
        ]
      : []),
    {
      name: "chromium",
      use: {
        browserName: "chromium",
        launchOptions: { args: ["--use-angle=swiftshader", "--enable-unsafe-swiftshader"] },
      },
    },
    { name: "firefox", use: { browserName: "firefox" } },
    { name: "webkit", use: { browserName: "webkit" } },
  ],
  webServer: {
    command: "pnpm dev --host 127.0.0.1 --port 5186 --strictPort",
    url: "http://127.0.0.1:5186",
    reuseExistingServer: !process.env.CI,
  },
});
