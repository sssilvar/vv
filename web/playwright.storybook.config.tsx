import { defineConfig } from "@playwright/test";

export default defineConfig({
  testDir: "stories",
  testMatch: "**/*.spec.tsx",
  workers: 1,
  retries: 0,
  timeout: 60_000,
  use: {
    baseURL: "http://127.0.0.1:6006",
    viewport: { width: 1280, height: 850 },
    screenshot: "only-on-failure",
    trace: "retain-on-failure",
    launchOptions: { args: ["--use-angle=swiftshader", "--enable-unsafe-swiftshader"] },
  },
  webServer: {
    command:
      "pnpm exec vite preview --outDir storybook-static --host 127.0.0.1 --port 6006 --strictPort",
    url: "http://127.0.0.1:6006",
    reuseExistingServer: !process.env.CI,
  },
});
