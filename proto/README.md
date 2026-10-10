# proto

Protobuf contracts, the single source of truth for every message that crosses a process or language boundary. C++ uses the generated types over gRPC, the browser reaches `ics-api` through Envoy gRPC-Web, and Python reads the same types for golden files and analysis.

**Language:** Protobuf. **Lead role:** Architect.

## Messages

[ICS-012](https://github.com/MatthewK84/ICS/issues/12) defined the contracts, all in package `ics.v1` under [`ics/v1/`](ics/v1):

| File | Messages | What they carry | Produced by |
|---|---|---|---|
| [`common.proto`](ics/v1/common.proto) | `GeodeticPoint`, `EnuVector`, `EnuCovariance`, `EntityRole`, `FileDigest` | Positions in the two range frames, their uncertainty, object roles and file digests | Shared |
| [`time_quality.proto`](ics/v1/time_quality.proto) | `TimeQuality` | A station's clock state, PTP and IRIG-B health, error bound and per-camera offsets | ics-timingd (ICS-019), strobe analyzer (ICS-029) |
| [`pli.proto`](ics/v1/pli.proto) | `PliRecord`, `PliEvent` | Vehicle positions, velocities and attitude with their time basis and σ; arming, modes, commands, status text and link loss | PLI adapters (ICS-021 to ICS-025), ics-plid (ICS-030) |
| [`pli_query.proto`](ics/v1/pli_query.proto) | `PliQueryService`, `QueryPliRequest`, `QueryPliResponse` | Queries for the stored PLI: records or events in a time range, of one entity if named, in batches | ics-plid (ICS-030) |
| [`time_quality_service.proto`](ics/v1/time_quality_service.proto) | `TimeQualityService`, `WatchTimeQualityRequest`, `WatchTimeQualityResponse` | The stream of a station's `TimeQuality` reports | ics-timingd (#150) |
| [`mount_sample.proto`](ics/v1/mount_sample.proto) | `MountSample` | Encoder-tagged axis angles and rates, mount mode and the sun interlock | Time-tagger (ICS-032), mount client (ICS-033) |
| [`camera_frame_meta.proto`](ics/v1/camera_frame_meta.proto) | `CameraFrameMeta` | When and how each frame was exposed | ics-camd (ICS-031) |
| [`trigger_event.proto`](ics/v1/trigger_event.proto) | `TriggerEvent` | Arming, closest-approach predictions, firing and faults | Trigger controller (ICS-040) |
| [`track.proto`](ics/v1/track.proto) | `Track`, `TrackState` | Estimated trajectories with position and velocity covariance | Endgame tracker (ICS-067), association (ICS-071) |
| [`fragment.proto`](ics/v1/fragment.proto) | `Fragment` | A debris piece's β, projected area, Cd and mass, each with σ | Estimation (ICS-073, ICS-075) |
| [`kill_assessment.proto`](ics/v1/kill_assessment.proto) | `KillAssessment` | Kill class, closest approach, miss distance ±σ, the evidence and thresholds, and evaluator overrides | Kill classifier (ICS-076), evaluator workflow (ICS-090) |
| [`footprint.proto`](ics/v1/footprint.proto) | `Footprint` | Ground-impact probability regions as WGS84 polygons | Footprint Monte Carlo (ICS-051, ICS-077) |
| [`run_record.proto`](ics/v1/run_record.proto) | `RunRecord` | One run's headline results, C4 measurements, flags, and a SHA-256 manifest of its parameters, models and artifacts | Run records (ICS-079) |

The contracts define messages. Each service defines its own gRPC service when it is built, as ics-plid's `PliQueryService` and ics-timingd's `TimeQualityService` (#150) do. Each is served over gRPC on a local Unix-domain socket; its C++ service code is generated at build time by the `grpc_cpp_plugin` of the Conan gRPC package the library comes from, not committed ([`cpp/README.md`](../cpp/README.md#pli-store)). `WatchTimeQualityRequest` has no fields, so it has no golden file (below): it is always written as no bytes at all.

[`third_party/`](third_party/README.md) holds protobuf files copied unchanged from other projects, which ICS reads but does not own: SAPIENT's BSI Flex 335 v2.0 messages, for the SAPIENT adapter ([ICS-024](https://github.com/MatthewK84/ICS/issues/24)). The conventions below are for ICS's own contracts and do not apply to them.

## Conventions

buf's COMMENTS rules require a comment on every message, field and enum value; the comment gives the unit and, where it matters, the frame.

**Units are in the field name.** Every physical quantity ends in its unit, in SI:

| Suffix | Unit | Example |
|---|---|---|
| `_utc_ns` | A time, UTC, in nanoseconds since the Unix epoch, as `int64` | `time_utc_ns` |
| `_ns` | A duration or time offset, in nanoseconds | `exposure_duration_ns` |
| `_m`, `_m2` | Metres, square metres | `miss_distance_m` |
| `_mps` | Metres per second | `velocity_enu_mps` |
| `_rad`, `_rad_per_s` | Radians, radians per second | `azimuth_rate_rad_per_s` |
| `_deg` | Degrees: geodetic latitude and longitude only | `latitude_deg` |
| `_kg`, `_kg_per_m2` | Kilograms, kilograms per square metre | `ballistic_coefficient_kg_per_m2` |
| `_px`, `_bytes`, `_count` | Pixels, bytes, a count | `width_px` |
| `_sigma_<unit>` | One-sigma uncertainty of the field before it | `mass_sigma_kg` |

Fields inside a shared type (`EnuVector`, `EnuCovariance`) take their unit from the field that holds them: `position_enu_m` is in metres and `velocity_covariance_m2_per_s2` in square metres per second squared. A measurement whose unit is data (`RunRecord.Measurement`, `KillAssessment.Threshold`) carries a UCUM unit code.

**Frames are types.** `GeodeticPoint` is WGS84 latitude, longitude and height above the ellipsoid. `EnuVector` and `EnuCovariance` are in the range's east-north-up frame, whose origin is the defended asset (`RunRecord.range_origin`). A geodetic point cannot be passed where an ENU vector is expected. The one exception is `PliRecord.Attitude`, which keeps MAVLink's body-to-north-east-down quaternion. [`docs/frames-and-time.md`](../docs/frames-and-time.md) defines the frames, heights and time scale, and [`golden/frames/`](../golden/frames) holds GeographicLib's conversion vectors for them (ICS-014).

**Time is UTC as `int64` nanoseconds** since the Unix epoch, without leap seconds, as POSIX time counts them. `google.protobuf.Timestamp` is not used.

**Presence.** A scalar that can be legitimately absent and whose zero is meaningful is `optional`, such as `PliRecord.horizontal_sigma_m` or `KillAssessment.contact_utc_ns`. Sub-messages always have presence.

**No maps.** Their entry order is not fixed, so the three languages could serialize the same message differently. Use a repeated message instead. The golden-file writer rejects any map field.

**Changes are additive.** Once on `main`, a field is never renamed, renumbered or retyped; `buf breaking` enforces this.

## Golden files

[`python/ics_golden/proto.py`](../python/ics_golden/proto.py) writes one instance of each top-level message, with every field set, to [`golden/proto/`](../golden/proto). The tests in each language prove they agree on the wire format:

- **Python** ([`python/tests/test_proto.py`](../python/tests/test_proto.py)): the committed files are current, and every field is set.
- **C++** ([`cpp/proto/test`](../cpp/proto/test/proto_golden_test.cpp)) and **TypeScript** ([`web/packages/ics-proto/test`](../web/packages/ics-proto/test/golden.test.ts)): each file parses with no unknown field and serializes back to the same bytes.

After changing a contract, regenerate the code (below) and then the golden files, from `python/`:

```sh
PYTHONPATH=gen uv run --locked python -m ics_golden.proto ../golden/proto
```

## Tooling

[ICS-011](https://github.com/MatthewK84/ICS/issues/11) set up the tooling:

| File | Purpose |
|---|---|
| [`buf.yaml`](buf.yaml) | The module: lint rules (STANDARD and COMMENTS) and breaking-change rules (FILE) |
| [`buf.gen.yaml`](buf.gen.yaml) | Code generation for C++ and Python (protoc's built-in generators) and TypeScript (protobuf-es) |
| [`buf.gen.third_party.yaml`](buf.gen.third_party.yaml) | C++ generation for `third_party/`, beside the ICS C++ |
| [`tools.txt`](tools.txt) | buf 1.73.0 and protoc 35.0, pinned by sha256 |
| [`third_party/tools.txt`](third_party/tools.txt) | The source commit and sha256 of each copied folder |
| [`check-proto.sh`](check-proto.sh) | What CI runs ([`proto.yml`](../.github/workflows/proto.yml)); see below |

The generated code is committed, in one package per language:

| Language | Package | Generated into | Runtime |
|---|---|---|---|
| C++ | CMake target `ics::proto` ([`cpp/proto`](../cpp/proto/CMakeLists.txt)), namespace `ics::v1` | `cpp/proto/gen/` | Conan `protobuf/7.35.0` |
| Python | modules `ics.v1.<file>_pb2` (for example `ics.v1.run_record_pb2`) | `python/gen/` | `protobuf==7.35.1`, with `types-protobuf` stubs for mypy |
| TypeScript | workspace package `@ics/proto` ([`web/packages/ics-proto`](../web/packages/ics-proto/src/index.ts)) | `web/packages/ics-proto/src/gen/` | `@bufbuild/protobuf` 2.15.0 |

protoc 35.0 must match both protobuf runtimes: C++ generated code compiles only against the same protobuf release, and the Python runtime must be at least as new. Upgrade `tools.txt`, `cpp/conanfile.py` and `python/pyproject.toml` together.

Generated code is not ICS-authored, so each language treats it like a dependency:

- the ICS lint rules, warnings-as-errors and suppression scans skip the `gen/` folders;
- the C++ headers are system headers to the code that includes them;
- mypy reads the Python stubs but does not report on them.

The code that uses the generated messages is checked as usual.

## Change a contract

From the repository root, with buf and protoc from `tools.txt` on the `PATH` and the web dependencies installed (`pnpm install --frozen-lockfile` in `web/`):

```sh
deploy/evidence/install-tools.sh /tmp/proto-tools proto/tools.txt && export PATH="/tmp/proto-tools:${PATH}"
buf format -w proto --exclude-path proto/third_party/sapient_msg && buf lint proto
buf generate proto --template proto/buf.gen.yaml --exclude-path proto/third_party/sapient_msg
buf generate proto --template proto/buf.gen.third_party.yaml --path proto/third_party/sapient_msg
proto/check-proto.sh origin/main
```

Then regenerate the golden files, as above.

`check-proto.sh` runs five checks:

1. `buf format` and `buf lint` must pass. The copied files in `third_party/` are linted with buf's MINIMAL rules only, and not formatted.
2. `buf breaking` must find no breaking change against the given commit.
3. `buf generate` must reproduce the committed code exactly.
4. Each folder in `third_party/` must match the sha256 in [`third_party/tools.txt`](third_party/tools.txt).
5. Each check above must reject its seeded defect:
   - a camelCase field name ([`policy/seeded/lint_violation.proto`](policy/seeded/lint_violation.proto));
   - a deleted field (`RunRecord.flags`);
   - an edited generated file;
   - an edited copied file.

`buf.yaml` ignores `ics/toolchain_check` in breaking-change checks: ICS-012 removed those two sample messages, which ICS-011 used to exercise the generators and nothing else used. Remove the entry once `main` no longer has them.
