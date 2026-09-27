import { createElement } from "react";
import { renderToStaticMarkup } from "react-dom/server";
import { describe, expect, it } from "vitest";

import { App } from "./App.tsx";

describe("App", () => {
  it("renders the heading and the canary station status", () => {
    const html = renderToStaticMarkup(createElement(App, { nowMs: 5_000 }));
    expect(html).toContain("<h1>ICS</h1>");
    expect(html).toContain("Station 1: Connected (5 s ago)");
  });
});
