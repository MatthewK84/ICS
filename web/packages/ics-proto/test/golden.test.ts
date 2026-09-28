// The TypeScript code generated from proto/ reads and writes every ICS message
// exactly as the Python reference does (ICS-012). Each golden file from
// python/ics_golden/proto.py must parse with no unknown field and serialize
// back to the same bytes.
import { readdirSync, readFileSync } from "node:fs";

import type { DescMessage } from "@bufbuild/protobuf";
import { describe, expect, it } from "vitest";

import {
  CameraFrameMetaSchema,
  create,
  FootprintSchema,
  FragmentSchema,
  fromBinary,
  KillAssessment_KillClass,
  KillAssessmentSchema,
  MountSampleSchema,
  PliEventSchema,
  PliRecordSchema,
  RunRecordSchema,
  TimeQualitySchema,
  toBinary,
  TrackSchema,
  TriggerEventSchema,
} from "../src/index.ts";

const GOLDEN_DIR = new URL("../../../../golden/proto/", import.meta.url);
const TIME_UTC_NS = 1_790_000_000_123_456_789n;

const GOLDEN: readonly (readonly [string, DescMessage])[] = [
  ["camera_frame_meta", CameraFrameMetaSchema],
  ["footprint", FootprintSchema],
  ["fragment", FragmentSchema],
  ["kill_assessment", KillAssessmentSchema],
  ["mount_sample", MountSampleSchema],
  ["pli_event", PliEventSchema],
  ["pli_record", PliRecordSchema],
  ["run_record", RunRecordSchema],
  ["time_quality", TimeQualitySchema],
  ["track", TrackSchema],
  ["trigger_event", TriggerEventSchema],
];

function readGolden(name: string): Uint8Array {
  return new Uint8Array(readFileSync(new URL(`${name}.binpb`, GOLDEN_DIR)));
}

function byName(left: string, right: string): number {
  return left.localeCompare(right);
}

describe("golden files from the Python reference", () => {
  it.each(GOLDEN)("%s parses with no unknown field and writes back the same bytes", (name, schema) => {
    const golden = readGolden(name);
    const message = fromBinary(schema, golden, { readUnknownFields: false });
    expect(toBinary(schema, message)).toEqual(golden);
  });

  it("has a test for every golden file", () => {
    const files = readdirSync(GOLDEN_DIR).map((file) => file.replace(/\.binpb$/, ""));
    expect(files.toSorted(byName)).toEqual(GOLDEN.map(([name]) => name).toSorted(byName));
  });
});

describe("@ics/proto", () => {
  it("keeps nanosecond times exact as bigint", () => {
    const event = create(TriggerEventSchema, { timeUtcNs: TIME_UTC_NS, channels: ["a", "b"] });
    const parsed = fromBinary(TriggerEventSchema, toBinary(TriggerEventSchema, event));
    expect(parsed.timeUtcNs).toBe(TIME_UTC_NS);
    expect(parsed.channels).toEqual(["a", "b"]);
  });

  it("keeps the presence of optional fields, even at zero", () => {
    const record = create(PliRecordSchema, { horizontalSigmaM: 0 });
    const parsed = fromBinary(PliRecordSchema, toBinary(PliRecordSchema, record));
    expect(parsed.horizontalSigmaM).toBe(0);
    expect(parsed.verticalSigmaM).toBeUndefined();
    expect(parsed.receivedUtcNs).toBeUndefined();
  });

  it("reads an empty message as all defaults", () => {
    const record = fromBinary(RunRecordSchema, new Uint8Array());
    expect(record.killAssessment).toBeUndefined();
    expect(record.flags).toEqual([]);
    expect(create(KillAssessmentSchema).computedClass).toBe(KillAssessment_KillClass.UNSPECIFIED);
  });
});
