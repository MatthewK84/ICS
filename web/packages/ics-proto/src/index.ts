// @ics/proto: TypeScript types for the ICS protobuf contracts (ICS-011).
// src/gen is generated from proto/ by buf and protobuf-es; never edit it by
// hand (see proto/README.md). The runtime functions are re-exported so callers
// need only this package.
export { create, equals, fromBinary, toBinary } from "@bufbuild/protobuf";

export * from "./gen/ics/toolchain_check/v1/sample_pb.ts";
export * from "./gen/ics/toolchain_check/v1/sample_set_pb.ts";
