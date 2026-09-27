# proto

Protobuf contracts, the single source of truth for every message that crosses a process or language boundary. C++ uses the generated types over gRPC, the browser reaches `ics-api` through Envoy gRPC-Web, and Python reads the same types for golden files and analysis.

**Language:** Protobuf. **Lead role:** Architect.

Planned messages: `TimeQuality`, `PliRecord`, `PliEvent`, `MountSample`, `CameraFrameMeta`, `TriggerEvent`, `Track`, `Fragment`, `KillAssessment`, `Footprint`, `RunRecord`.

Rules:

- Document every field with its units. buf's COMMENTS rules require a comment on every message, field and enum value.
- `buf lint` and `buf breaking` must pass; changes are additive.
- Frames and time follow the conventions fixed in [ICS-014](https://github.com/MatthewK84/ICS/issues/14): WGS84, range ENU origin at the defended asset, UTC as int64 nanoseconds, ellipsoid heights.
- Never edit generated code by hand; change the `.proto` file and regenerate.

## Tooling

[ICS-011](https://github.com/MatthewK84/ICS/issues/11) set up the tooling:

| File | Purpose |
|---|---|
| [`buf.yaml`](buf.yaml) | The module: lint rules (STANDARD and COMMENTS) and breaking-change rules (FILE) |
| [`buf.gen.yaml`](buf.gen.yaml) | Code generation for C++ and Python (protoc's built-in generators) and TypeScript (protobuf-es) |
| [`tools.txt`](tools.txt) | buf 1.73.0 and protoc 35.0, pinned by sha256 |
| [`check-proto.sh`](check-proto.sh) | What CI runs ([`proto.yml`](../.github/workflows/proto.yml)); see below |
| `ics/toolchain_check/v1/` | Two small messages that exercise the generators until the ICS messages arrive in `ics.v1` ([ICS-012](https://github.com/MatthewK84/ICS/issues/12)) |

The generated code is committed, in one package per language:

| Language | Package | Generated into | Runtime |
|---|---|---|---|
| C++ | CMake target `ics::proto` ([`cpp/proto`](../cpp/proto/CMakeLists.txt)) | `cpp/proto/gen/` | Conan `protobuf/7.35.0` |
| Python | modules `ics.<package>` (for example `ics.toolchain_check.v1.sample_pb2`) | `python/gen/` | `protobuf==7.35.1`, with `types-protobuf` stubs for mypy |
| TypeScript | workspace package `@ics/proto` ([`web/packages/ics-proto`](../web/packages/ics-proto/src/index.ts)) | `web/packages/ics-proto/src/gen/` | `@bufbuild/protobuf` 2.15.0 |

protoc 35.0 must match both protobuf runtimes: C++ generated code compiles only against the same protobuf release, and the Python runtime must be at least as new. Upgrade `tools.txt`, `cpp/conanfile.py` and `python/pyproject.toml` together.

Generated code is not ICS-authored, so each language treats it like a dependency:

- the ICS lint rules, warnings-as-errors and suppression scans skip the `gen/` folders;
- the C++ headers are system headers to the code that includes them;
- mypy reads the Python stubs but does not report on them.

The code that uses the generated messages is checked as usual, and each language has a round-trip test: `cpp/proto/test`, `python/tests/test_proto.py` and `web/packages/ics-proto/test`.

## Change a contract

From the repository root, with buf and protoc from `tools.txt` on the `PATH` and the web dependencies installed (`pnpm install --frozen-lockfile` in `web/`):

```sh
deploy/evidence/install-tools.sh /tmp/proto-tools proto/tools.txt && export PATH="/tmp/proto-tools:${PATH}"
buf format -w proto && buf lint proto
buf generate proto --template proto/buf.gen.yaml
proto/check-proto.sh origin/main
```

`check-proto.sh` runs four checks:

1. `buf format` and `buf lint` must pass.
2. `buf breaking` must find no breaking change against the given commit.
3. `buf generate` must reproduce the committed code exactly.
4. Each check above must reject its seeded defect:
   - a camelCase field name ([`policy/seeded/lint_violation.proto`](policy/seeded/lint_violation.proto));
   - a deleted field;
   - an edited generated file.
