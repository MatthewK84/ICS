// Vite build for the ICS web app (ICS-007). The preview server serves the
// production build to the Playwright tests.
import react from "@vitejs/plugin-react";
import { defineConfig } from "vite";

export default defineConfig({
  plugins: [react()],
  build: { sourcemap: true },
  preview: { host: "127.0.0.1", port: 4173, strictPort: true },
});
