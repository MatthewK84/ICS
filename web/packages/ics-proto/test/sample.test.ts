import { describe, expect, it } from "vitest";

import {
  create,
  equals,
  fromBinary,
  type SampleSet,
  SampleSchema,
  SampleSetSchema,
  Sample_Kind,
  toBinary,
} from "../src/index.ts";

const TIME_UTC_NS = 1_790_000_000_123_456_789n;

function sampleSet(): SampleSet {
  return create(SampleSetSchema, {
    stationId: "north-1",
    samples: [
      create(SampleSchema, {
        timeUtcNs: TIME_UTC_NS,
        kind: Sample_Kind.RANGE,
        values: [1523.5, 1524.25],
        position: { eastM: 10, northM: -5, upM: 2.5 },
        detail: { case: "readingCount", value: 4 },
      }),
      create(SampleSchema, { kind: Sample_Kind.ANGLE, detail: { case: "note", value: "sun in view" } }),
    ],
  });
}

describe("@ics/proto", () => {
  it("round-trips a sample set through the binary format", () => {
    const original = sampleSet();
    const parsed = fromBinary(SampleSetSchema, toBinary(SampleSetSchema, original));
    expect(equals(SampleSetSchema, parsed, original)).toBe(true);
    expect(parsed.samples[1]?.detail).toEqual({ case: "note", value: "sun in view" });
  });

  it("keeps nanosecond times exact as bigint", () => {
    const parsed = fromBinary(SampleSetSchema, toBinary(SampleSetSchema, sampleSet()));
    expect(parsed.samples[0]?.timeUtcNs).toBe(TIME_UTC_NS);
  });

  it("reads an empty message as all defaults", () => {
    const parsed = fromBinary(SampleSchema, new Uint8Array());
    expect(parsed.kind).toBe(Sample_Kind.UNSPECIFIED);
    expect(parsed.detail.case).toBeUndefined();
  });
});
