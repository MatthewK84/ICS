// End-to-end tests for the ICS web app (ICS-007): Chromium against the
// production build served by "vite preview". Run "pnpm run build" first.
import { defineConfig, devices } from "@playwright/test";

const BASE_URL = "http://127.0.0.1:4173";

export default defineConfig({
  testDir: "e2e",
  forbidOnly: true,
  retries: 0,
  reporter: [["list"]],
  use: { baseURL: BASE_URL },
  projects: [{ name: "chromium", use: { ...devices["Desktop Chrome"] } }],
  // Vite is started directly: through "pnpm exec", the stop signal does not
  // reach it and the run hangs after the tests finish.
  webServer: {
    command: "node node_modules/vite/bin/vite.js preview",
    url: BASE_URL,
    reuseExistingServer: false,
    timeout: 60_000,
  },
});
