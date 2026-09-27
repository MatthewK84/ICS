// Browser entry point. Covered by the Playwright tests, not by Vitest.
import { StrictMode } from "react";
import { createRoot } from "react-dom/client";

import { App } from "./App.tsx";

const container = document.getElementById("root");
if (container === null) {
  throw new Error("index.html has no #root element");
}
createRoot(container).render(
  <StrictMode>
    <App nowMs={Date.now()} />
  </StrictMode>,
);
