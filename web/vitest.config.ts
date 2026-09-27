// Unit tests for every workspace package (ICS-007), with at least 80% coverage
// of UI and tooling code. Browser entry points are covered by Playwright.
import { defineConfig } from "vitest/config";

export default defineConfig({
  test: {
    include: ["apps/*/src/**/*.test.ts", "packages/*/test/**/*.test.ts"],
    environment: "node",
    coverage: {
      provider: "v8",
      include: ["apps/*/src/**", "packages/*/src/**"],
      exclude: ["**/*.test.ts", "apps/*/src/main.tsx", "packages/ics-proto/src/gen/**"],
      thresholds: { lines: 80, functions: 80, branches: 80, statements: 80 },
    },
  },
});
