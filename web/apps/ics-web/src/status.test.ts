import { describe, expect, it } from "vitest";

import { describeLink, type LinkState } from "./status.ts";

describe("describeLink", () => {
  it.each<[LinkState, string]>([
    ["connected", "Station 2: Connected (3 s ago)"],
    ["degraded", "Station 2: Degraded (3 s ago)"],
    ["lost", "Station 2: Lost (3 s ago)"],
  ])("labels the %s state", (state, expected) => {
    expect(describeLink({ station: "Station 2", state, lastUpdateMs: 1_000 }, 4_999)).toBe(expected);
  });

  it("reads a last update in the future as 0 s", () => {
    expect(describeLink({ station: "S", state: "lost", lastUpdateMs: 9_000 }, 1_000)).toBe("S: Lost (0 s ago)");
  });
});
