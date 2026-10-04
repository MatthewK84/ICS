# cpp

Every real-time and production data path: timing, PLI, cameras, mounts, triggering, the processing pipeline, run records and the query API. C++ never does model fitting or exploratory analysis; that belongs in `python/`.

**Language:** C++. **Lead roles:** Systems engineer (timing, PLI, camera I/O), Controls engineer (mount, encoder, tracker, trigger), CV and estimation engineers (registration through footprint).

Planned layout:

```text
cpp/
  common/ logging/ config/ frames/ camera_io/ timing/ capture/ trigger/
  pli/ mount/ tracker/ encoder/
  endgame/ registration/ detection/ association/ estimation/
  killclass/ footprint/ record/ api/
  services/   ics-timingd ics-capd ics-plid ics-mountd ics-trigd ics-camd
              ics-api ics-recordd ics-pipeline
```

Rules (enforced in CI by [ICS-005](https://github.com/MatthewK84/ICS/issues/5) and [ICS-008](https://github.com/MatthewK84/ICS/issues/8); see [Power-of-Ten checks](#power-of-ten-checks)):

- No exceptions on real-time paths; fallible calls return `ics::Result` (a `tl::expected`), which is `[[nodiscard]]`. See [Common library](#common-library).
- No recursion, functions of 50 lines or fewer, no owning raw pointers, no mutable globals, no `NOLINT`.
- No allocation after initialization on real-time paths.
- Mount motion, the sun interlock and triggering live only here, never in the web UI.
- Python parameter files and ONNX models are loaded at startup, and their SHA-256 hashes go into every run record.

## Build

Build inside the `ics-cpp` image (see [deploy/toolchain](../deploy/toolchain/README.md)). Each preset in [`CMakePresets.json`](CMakePresets.json) pairs a compiler, `gcc` or `clang`, with a variant, `debug`, `release`, `asan`, `ubsan` or `tsan`; `clang-fuzz` builds the fuzz targets:

```sh
deploy/toolchain/conan-install.sh gcc-release   # dependencies from cpp/conan.lock
cd cpp
cmake --preset gcc-release
cmake --build --preset gcc-release
ctest --preset gcc-release
```

Builds are reproducible: `deploy/toolchain/check-reproducible.sh gcc` (or `clang`) builds the `toolchain_check` sample twice and requires byte-identical outputs. The flags that make this work are in [`cmake/Reproducible.cmake`](cmake/Reproducible.cmake).

The protobuf messages generated from [`proto/`](../proto/README.md) are the `ics::proto` library in [`proto/`](proto/CMakeLists.txt). Its code is generated and committed, never edited, and it is built without the ICS warning flags, like a dependency.

Dependencies are pinned in `conan.lock`. After adding a requirement to `conanfile.py`, add its pins with `conan lock create cpp --profile:all cpp/conan/profiles/gcc13 --lockfile cpp/conan.lock --lockfile-partial --lockfile-out cpp/conan.lock`, which keeps every existing pin, and record the new dependency in the [dependency register](../docs/dependency-register.md). Without access to Conan Center, push the change: the C++ toolchain workflow prints the updated lockfile to commit.

## Common library

[`common/`](common) is `ics::common` ([ICS-015](https://github.com/MatthewK84/ICS/issues/15), [ICS-016](https://github.com/MatthewK84/ICS/issues/16)), which every real-time component links:

| Header | Provides |
|---|---|
| [`error.hpp`](common/include/ics/common/error.hpp) | `ics::Error`, one enum of failure codes for all of ICS; `Result<T>` (`tl::expected<T, Error>`), `Status` (`Result<void>`) and `fail(error)`. A result is `[[nodiscard]]`, so ignoring one does not compile. It cannot hold a reference: return a `std::reference_wrapper` |
| [`check.hpp`](common/include/ics/common/check.hpp) | `ics::check(condition)`, the ICS assertion. It returns the condition and, when it is false, writes the caller's file, line and function to stderr without allocating; the caller recovers, normally by returning an error |
| [`static_vector.hpp`](common/include/ics/common/static_vector.hpp), [`ring_buffer.hpp`](common/include/ics/common/ring_buffer.hpp), [`fixed_pool.hpp`](common/include/ics/common/fixed_pool.hpp) | Fixed-capacity containers that never allocate: `StaticVector<T, N>`, the first-in, first-out `RingBuffer<T, N>`, and `FixedPool<T, N>`, whose handles carry a generation so a released one is refused. A full, empty, out-of-range or stale case returns an error; nothing throws or overwrites |
| [`storable.hpp`](common/include/ics/common/storable.hpp) | `Storable`, what the containers need from an element type: default construction, move construction and move assignment that cannot throw |
| [`units.hpp`](common/include/ics/common/units.hpp) | Strong unit types: `Meters`, `Radians` and `Degrees` are a `double` tagged with its unit, so mixing units, or passing a bare `double`, does not compile; `to_radians`, `to_degrees` and `wrap_to_pi` convert. Time is `std::chrono`: `UtcTime` (`sys_time<nanoseconds>`, POSIX-counted UTC as in [docs/frames-and-time.md](../docs/frames-and-time.md)) and `Duration` (`nanoseconds`), with `utc_from_ns` and `to_utc_ns` for `_utc_ns` fields |

How to use them:

- Use `ics::check` for a condition that holds unless there is a bug or an overload the system was sized to avoid, such as a push to a full buffer. An outcome the caller expects, such as a polling reader finding the ring buffer empty, is an ordinary branch.
- The containers are not thread-safe.
- Tests instantiate each container template explicitly (`template class ics::RingBuffer<int, 3>;`), so a member no test calls shows up as uncovered.
- The headers build with `-fno-exceptions`, as real-time targets will; [`common/test/no_exceptions_check.cpp`](common/test/no_exceptions_check.cpp) proves it.

## Logging

[`logging/`](logging) is `ics::logging` (ICS-016). A service makes one `Logger` at start-up, with `Logger::to_stderr(service, threshold)`, and passes it by reference; there is no global logger. Each call writes one JSON line through spdlog to stderr, which journald captures:

```cpp
logger.warn("frame_dropped", {{"camera", "north"}, {"count", 3}});
// {"ts":"2026-10-01T09:53:50.123456789Z","level":"warn","service":"ics-camd","event":"frame_dropped","camera":"north","count":3}
```

- Field values are integers, floating-point numbers, booleans or text. Keys are `lower_snake_case` and must not repeat `ts`, `level`, `service` or `event`; a field with a bad key fails an `ics::check` and is left out.
- Logging formats text and may allocate, so it is not for real-time paths. Real-time code counts or queues what happened, and a non-real-time thread logs it.

## Config

[`config/`](config) is `ics::config` (ICS-016). A service reads its TOML file once at start-up against a schema written as code, and exits when it is invalid, naming every bad field:

```cpp
const auto config = ics::config::read_file(path, &ics::config::read_logging);
if (!config) {
  std::fputs(ics::config::format_errors(path.string(), config.error()).c_str(), stderr);
  return EXIT_FAILURE;
}
// app.toml: log.level: must be one of debug, info, warn, error, not "verbose"
// app.toml: log.colour: is not a known setting
```

- A schema is a function that takes a `Reader&`. Its getters check type and range, and a setting with a unit must carry the unit in its name, as proto fields do: `duration("timeout_ns", …)`, `meters`, `radians`, `degrees`. `texts(key, min, max)` reads a list of text, such as `ics-capd`'s `interfaces`. Every setting is required, and one the schema never reads is reported as unknown, since it is likely a typo.
- Files are written in a TOML subset, defined in [`subset.hpp`](config/include/ics/config/subset.hpp): printable ASCII, tables of bare keys, and settings holding text, decimal numbers, booleans or one-line lists of those. `parse()` checks the subset before toml++ reads the text. Fuzzing found that toml++ 3.4.0 has undefined behaviour on some invalid TOML, such as a non-ASCII character or `=` straight after a table's `[`, and its unreleased main branch and toml11 4.4.0 failed fuzzing too.
- [`logging_config.hpp`](config/include/ics/config/logging_config.hpp) reads the `[log]` table every service has: `level` and `service`.

## Frames

[`frames/`](frames) is `ics::frames` (ICS-017). It converts positions between the frames that [docs/frames-and-time.md](../docs/frames-and-time.md) defines, and matches GeographicLib's golden vectors in [`golden/frames/`](../golden/frames) to within their printed precision:

| Header | Provides |
|---|---|
| [`geodetic.hpp`](frames/include/ics/frames/geodetic.hpp) | `Geodetic`, a WGS84 point made only through `Geodetic::make`. It refuses a value that is not finite or a latitude beyond a pole, and wraps longitude into [−180°, 180°). Also `to_ecef` and `to_geodetic`; the inverse is exact everywhere, including at the poles and near the Earth's centre |
| [`enu.hpp`](frames/include/ics/frames/enu.hpp) | `EnuFrame`, the range east-north-up frame of an origin (GeographicLib's `LocalCartesian`). It converts positions, and `rotate` turns vectors of any unit, such as velocities, between ECEF and ENU axes |
| [`egm96.hpp`](frames/include/ics/frames/egm96.hpp) | `Egm96`, the geoid that defines MSL. `Egm96::load` reads the `egm96-5` grid that the images install at `Egm96::kDefaultPath`. Then `geoid_height`, `from_msl` (h = H + N) and `msl_height` give heights |
| [`wgs84.hpp`](frames/include/ics/frames/wgs84.hpp) | The ellipsoid's constants |

- **Real-time use:** the conversions and geoid lookups are `noexcept`, allocate nothing (the tests run them inside `NoAllocationScope`) and are safe from several threads. Load the 19 MB grid once at start-up.
- **Ported code:** the angle reductions, the inverse conversion and the geoid interpolation are ported from GeographicLib 2.3, under its MIT License ([`frames/GEOGRAPHICLIB-LICENSE.txt`](frames/GEOGRAPHICLIB-LICENSE.txt)). The interpolation is GeographicLib's 12-point cubic fit, with its stencil tables copied unchanged. Bilinear interpolation would be off by up to 0.14 m, against the 1 mm the golden vectors require.
- **Tests:** the frames tests read the golden CSV files and the installed grid, at paths CMake passes in (`ICS_GEOID_DIR`, which defaults to the images' `/usr/share/GeographicLib/geoids`). They fail rather than skip when the grid is missing.

## Timing

[`services/timingd/`](services/timingd) is `ics-timingd` (ICS-019), built on `ics::timing` in [`timing/`](timing). It reads the station's PTP state from `ptp4l` and publishes `ics.v1.TimeQuality` reports to the processes on the station that need them.

Each poll interval, `ics-timingd` sends GET requests for four data sets (current, parent, time properties and port) to `ptp4l`'s read-only management socket (`uds_ro_address`), and matches the answers to its requests by sequence number. [`ptp_management.hpp`](timing/include/ics/timing/ptp_management.hpp) encodes the requests and decodes the answers (IEEE 1588-2008 clause 15). The decoder's tests and its fuzz target's seed corpus are answers captured from `ptp4l` 4.0.

| Clock state | When |
|---|---|
| `locked` | The port is `SLAVE` and the grandmaster announces clock class 6, time traceable |
| `holdover` | The port is `SLAVE` and the grandmaster announces class 7, or one of the ITU-T G.8275 holdover classes (135, 140 to 160) |
| `free_running` | Anything else, including a `ptp4l` that does not answer within the poll interval |

The report's `error_bound_ns` is a model, not a measurement: |offset| plus the accuracy the grandmaster announces (IEEE 1588-2019 Table 5) plus `asymmetry_bound_ns` when locked; the same plus `holdover_drift_ns_per_s` times the time in holdover; and unbounded, written as INT64_MAX, when free-running. Holdover is timed from the first poll that shows it. PTP does not carry `gnss_satellite_count` or `irig_b_locked`, so they stay 0 and false, and `camera_offsets` stays empty until the strobe calibration fills it.

- **Reports.** The build plan has `ics-timingd` serve reports as a gRPC server stream. gRPC is not in the toolchain yet, because Conan Center's gRPC package needs an older protobuf than the 7.35.0 the lockfile pins. Until it is, [`publisher.hpp`](timing/include/ics/timing/publisher.hpp) sends each report to every subscriber on a local `SOCK_SEQPACKET` socket (`publish_socket`), one serialized `TimeQuality` per message, which is a server stream's shape. Up to 16 subscribers; one that cannot take a report at once is dropped and must reconnect.
- **Logs.** `started`; `clock_state` when the state changes, with `state`, `ptp_offset_ns` and `error_bound_ns`; `ptp4l_answering` and, as a warning, `ptp4l_unavailable` when `ptp4l` starts or stops answering; `start_failed`; and `stopped`, with the `signal`.
- **Running.** `ics-timingd` takes no arguments. It reads `/etc/ics/ics-timingd.toml`, a config like [`ics-timingd.toml`](services/timingd/ics-timingd.toml); the path is fixed so the service only ever opens that one file (CodeQL's path-injection rule rejects a path taken from the command line). It runs until SIGINT or SIGTERM and exits 0. It exits 1 when it cannot open a socket, and 2 for an argument or a bad config file, which it reports field by field.
- **Allocation.** Once running, a poll that changes nothing allocates nothing: the tests run one inside `NoAllocationScope`. Only log lines allocate.
- **Bench.** The [timing bench](../deploy/timing-bench/README.md) checks, every night, that holdover is flagged within 1 s of GNSS loss, and describes the same check on real hardware.

## Capture

[`services/capd/`](services/capd) is `ics-capd` (ICS-020), built on `ics::capture` in [`capture/`](capture). It captures every packet on the station's TAP ports to pcap files, so the PLI adapters (ICS-021 on) and the run record can replay exactly what arrived.

| Header | Provides |
|---|---|
| [`capture.hpp`](capture/include/ics/capture/capture.hpp) | `Capture`: libpcap, live from an interface or replayed from a file, handing packets to a `PacketSink` without blocking, with libpcap's received and dropped counters |
| [`rotating_writer.hpp`](capture/include/ics/capture/rotating_writer.hpp) | `RotatingWriter`: one interface's packets to a series of pcap files, each closed by age or size, SHA-256 hashed as it is written, with a `.sha256` file in `sha256sum` format beside it |
| [`pcap_format.hpp`](capture/include/ics/capture/pcap_format.hpp) | The classic pcap headers, with the nanosecond magic number |
| [`datagram.hpp`](capture/include/ics/capture/datagram.hpp) | `udp_datagram`: the UDP datagram in a captured Ethernet frame (at most one 802.1Q tag, IPv4, no fragments), which the PLI adapters read |
| [`sha256.hpp`](capture/include/ics/capture/sha256.hpp), [`output_file.hpp`](capture/include/ics/capture/output_file.hpp) | SHA-256 through OpenSSL's EVP interface, so a FIPS provider computes it when the station loads one; a file created only if nothing is at its path, so a capture is never overwritten |

- **Files.** One series per interface, named `<interface>-<UTC time it opened>.pcap`, such as `tap0-20261003T171500.123456789Z.pcap`, in `folder`. Classic pcap with nanosecond time stamps, which `tcpdump`, Wireshark and libpcap read. A file closes after `rotate_interval_ns`, or before a packet would take it past `rotate_bytes`; its bytes are synced, then its `.sha256` is written and synced. `ics-capd` writes the bytes itself rather than through `pcap_dump`, so it hashes them as they go, never reads a file back, and sees every write error. A file with no packets is still written: it shows the capture ran. Nothing is ever deleted; retention is the station's job.
- **Time stamps.** `timestamps = "adapter"` takes each packet's time from the NIC's PTP hardware clock, which `ptp4l` keeps on TAI, and subtracts TAI − UTC, read once from `ptp4l` (`timePropertiesDS.currentUtcOffset`, which must be marked valid) at start-up; a leap second needs a restart. libpcap asks Linux for the raw hardware stamp (`adapter_unsynced`): the converted `adapter` stamp is no longer supported by the kernel. An interface that cannot give hardware stamps fails at start, never falling back to host stamps. `timestamps = "host"` uses the kernel's clock, for interfaces without a hardware clock, such as the bench's veth pair.
- **Capture.** Promiscuous mode, since a TAP port's packets are addressed to other hosts, and `buffer_bytes` of kernel ring per interface. One thread polls every interface and writes what waits, up to 4,096 packets per interface per round. Up to 4 interfaces, all in one `ics-capd`.
- **Logs.** `started`; `file_closed` with `interface`, `path`, `sha256`, `packets` and `bytes`, then `counters` with the packets the kernel `received`, `dropped` and `interface_dropped` while that file was open; `drops`, as an error, when any packet was dropped, but capture goes on; `capture_failed` and `close_failed`; `start_failed`, with libpcap's `reason`; and `stopped`, with the `signal`.
- **Running.** `ics-capd` takes no arguments. It reads `/etc/ics/ics-capd.toml`, a config like [`ics-capd.toml`](services/capd/ics-capd.toml). It needs `CAP_NET_RAW`, and `CAP_NET_ADMIN` for hardware time stamps. It runs until SIGINT or SIGTERM, closes and hashes its files, and exits 0. A capture or write error, such as a full disk, also closes and hashes the files it can, and exits 1 so systemd restarts it; it exits 1 too when it cannot start, and 2 for an argument or a bad config file.
- **Allocation.** Writing a packet allocates nothing; opening a file and logging do.
- **Tests.** The live tests capture on the loopback interface, so they need `CAP_NET_RAW`, as the tests have when run as root in the `ics-cpp` container.
- **Bench.** The [capture bench](../deploy/capture-bench/README.md) checks, every night, an accelerated 24 h capture with zero drops, every packet in the files and every hash matching, and describes the 24 h run on real hardware.

## MAVLink

[`mavlink/`](mavlink) is `ics::mavlink` (ICS-021), the first PLI adapter. It reads the MAVLink a TAP port carries and turns it into `ics.v1.PliRecord` and `PliEvent` ([`pli.proto`](../proto/ics/v1/pli.proto)), for `ics-plid` (ICS-030) to serve.

| Header | Provides |
|---|---|
| [`frame.hpp`](mavlink/include/ics/mavlink/frame.hpp) | `FrameReader`: the MAVLink 1 and 2 frames in a datagram, each checked with the X.25 checksum and its message's CRC_EXTRA; frames of other messages are stepped over, and bytes that start no frame are counted and skipped |
| [`messages.hpp`](mavlink/include/ics/mavlink/messages.hpp) | `decode`: `HEARTBEAT`, `SYSTEM_TIME`, `GPS_RAW_INT`, `ATTITUDE_QUATERNION`, `GLOBAL_POSITION_INT`, `COMMAND_LONG`, `COMMAND_ACK` and `STATUSTEXT`, field for field, extensions included |
| [`adapter.hpp`](mavlink/include/ics/mavlink/adapter.hpp), [`modes.hpp`](mavlink/include/ics/mavlink/modes.hpp) | `Adapter`: records and events from frames; the flight modes of PX4 and ArduCopter by name |

- **Records.** One per `GLOBAL_POSITION_INT` from a vehicle's autopilot (component 1). The height above mean sea level becomes a height above the ellipsoid through EGM96 ([Frames](#frames)); the velocity is rotated from the vehicle's north-east-down axes into the range ENU frame. The fix type and one-sigma accuracies of the vehicle's last `GPS_RAW_INT`, and the attitude of its last `ATTITUDE_QUATERNION`, are added when received within 1 s (`max_age`). `entity_id` is the MAVLink system ID; the role comes from the settings.
- **Time.** Each `SYSTEM_TIME` that carries UTC gives the vehicle's boot-to-UTC offset, and a record is valid at its `time_boot_ms` plus the last offset (`PLI_TIME_BASIS_VEHICLE_GNSS`). Before a vehicle sends UTC, its records take the time they were received (`PLI_TIME_BASIS_RECEIPT`), the capture's time stamp. There is no drift fit yet (CPP-11): on the SITL rig, ArduCopter's positions are valid about 230 ms before they arrive, drifting by about 0.1 ms each second.
- **Events.** Arming and disarming and mode changes from `HEARTBEAT`; each `COMMAND_LONG`, for its target; each `COMMAND_ACK` and `STATUSTEXT` chunk; and a link lost when a vehicle's heartbeats stop for longer than `link_timeout` (3 s), checked by `tick`, and restored at the next. MAVLink stamps none of them, so they take the time they were received.
- **Signing.** A signed frame is read with its signature skipped: ICS listens on a TAP port and holds no key.
- **Replay.** [`testing/mavlink_replay`](testing/mavlink_replay/main.cpp) is `ics-mavlink-replay`, which runs a pcap file through the adapter and writes JSON lines. Every night the [SITL rig workflow](../.github/workflows/sitl.yml) replays each engagement's TAP capture and checks it against the rig's truth log ([`deploy/sitl`](../deploy/sitl/README.md#mavlink-replay)). The tests replay part of one such capture ([`test/fixtures`](mavlink/test/fixtures/README.md)).

## Cursor on Target

[`cot/`](cot) is `ics::cot` (ICS-022), the second PLI adapter. It reads the Cursor on Target (CoT) XML that a TAP port carries in UDP datagrams, such as the situational-awareness reports ATAK and WinTAK send, and turns each position into an `ics.v1.PliRecord` ([`pli.proto`](../proto/ics/v1/pli.proto)).

| Header | Provides |
|---|---|
| [`event.hpp`](cot/include/ics/cot/event.hpp) | `read_events`: the `<event>` elements in a datagram's payload, one or several, read with pugixml; malformed payloads, other elements and events without a usable point are counted |
| [`time.hpp`](cot/include/ics/cot/time.hpp) | `parse_cot_time`: CoT's `xs:dateTime` times, with a fraction of up to nine digits and `Z` or an offset |
| [`uncertainty.hpp`](cot/include/ics/cot/uncertainty.hpp) | `sigma_factors`, `sigma`: CoT's `ce` and `le` as one-sigma errors |
| [`adapter.hpp`](cot/include/ics/cot/adapter.hpp) | `Adapter`: a record from each position |

- **Records.** One per atom with a usable point: an event whose type starts `a-`. Chat, markers, deletes and every other event give none, and staleness is left to `ics-plid` (ICS-030). `entity_id` is the uid; the role comes from the settings, and is `ENTITY_ROLE_OTHER` for a uid not listed. The point's `hae` is already above the WGS84 ellipsoid. A `<track>`'s course and speed become a horizontal velocity in the range ENU frame. A machine GPS position (`how` starting `m-g`) is a 3D fix, or 2D when `hae` is unknown (`9999999`), and the height is then 0; any other `how`, such as a human estimate, is `FIX_TYPE_OTHER`.
- **Errors.** `ce` and `le` are read as circular and linear errors at `ce_probability` and `le_probability`, 0.90 by default (CE90 and LE90): the horizontal sigma is `ce / sqrt(-2 ln(1 - p))`, and the vertical sigma is `le` over the normal quantile at `(1 + p) / 2`. CoT does not fix the probability, so set it to what the senders use. An unknown `ce` or `le`, or an unknown `hae`, leaves its sigma unset.
- **Time.** A record is valid at the event's `time` (`PLI_TIME_BASIS_VEHICLE_GNSS`) when that is within `max_skew` (30 s) of the capture's time stamp. Otherwise, and when `time` is missing or malformed, it takes the capture's time stamp (`PLI_TIME_BASIS_RECEIPT`), which flags a sender whose clock is wrong.
- **Transport.** CoT XML in UDP datagrams, one or more events each. Not yet read: CoT over TCP or TLS streams, such as a TAK Server's, and the TAK Protocol's protobuf payloads.
- **XML.** pugixml, built without exceptions, reads no DTD and expands no entities beyond XML's own, so a hostile payload cannot reach files or the network, or grow without bound.
- **Tests.** The [sample feeds](cot/test/samples/README.md) are synthetic, written from the public CoT schema: ATAK, WinTAK through a TAK Server, a UAS, a human estimate, chat and a delete, and malformed and invalid payloads. The tests read each one directly, and again from a pcap file written as `ics-capd` writes one. The fuzz target reads its input as a captured frame and as a payload.

## Power-of-Ten checks

The C++ rules from the coding-standards table in [docs/build-plan.md](../docs/build-plan.md) are enforced by tools, not review (ICS-005):

| Rule | Enforced by |
|---|---|
| No goto; no recursion; functions of 50 lines or fewer; no owning raw pointers or malloc; `std::span` over pointer arithmetic; no mutable globals | clang-tidy, configured in [`.clang-tidy`](.clang-tidy); every finding is an error |
| `[[nodiscard]]` results are used; no implicit narrowing | `-Wall -Wextra -Wpedantic -Wconversion -Werror` for GCC and Clang, in [`cmake/Warnings.cmake`](cmake/Warnings.cmake) |
| Defects such as out-of-bounds access or uninitialized reads | cppcheck 2.13 (warning, style, performance and portability checks) over the whole build; every finding is an error |
| No suppressions | [`policy/check-policy.sh`](policy/check-policy.sh) rejects `NOLINT`, `cppcheck-suppress`, `diagnostic ignored` pragmas, `-Wno-` flags and any extra `.clang-tidy` file |
| No allocation after initialization | [`ics::testing::NoAllocationScope`](testing/alloc_guard/include/ics/testing/no_allocation_scope.hpp) fails a test that allocates inside it |

Run all of it the way CI does, inside the `ics-cpp` image:

```sh
cpp/policy/check-policy.sh
```

The script also proves each rule still fires: every file in [`policy/seeded/`](policy/seeded) breaks exactly one rule and must be rejected with the diagnostic named on its first line. The seeds build only with `-DICS_POLICY_SEEDS=ON`.

Add tests with `ics_add_gtest(name SOURCES … LIBRARIES …)` from [`cmake/Testing.cmake`](cmake/Testing.cmake). It links GoogleTest and the allocation guard, which replaces the global `operator new` and `delete` so that allocations are counted. Open a `NoAllocationScope` once a component is initialized and run its steady-state path inside it.

Two exceptions are deliberate:

- The allocation hook, [`testing/alloc_guard/src/allocation_hook.cpp`](testing/alloc_guard/src/allocation_hook.cpp), is the only file allowed to call `malloc`; its own `.clang-tidy` turns off `cppcoreguidelines-no-malloc` there and nowhere else. It marks ownership with `gsl::owner`, so `cppcoreguidelines-owning-memory` still applies.
- The `clang-tsan` preset links Clang's shared TSan runtime (`-shared-libsan`), because the static runtime defines `operator new` and `delete` itself and would clash with the allocation hook. GCC's TSan runtime is already shared.

Not enforced here: "no exceptions on real-time paths" needs `-fno-exceptions` on the real-time targets once they exist; the common headers already build that way. Coverage is gated only for the code listed in [`policy/coverage-gates.txt`](policy/coverage-gates.txt) (see [Test stages](#test-stages)), so other code is not yet measured against the Definition of Done's 90%.

## Test stages

The dynamic stages run in CI on every change to `cpp/` (ICS-008), through [`policy/check-dynamic.sh`](policy/check-dynamic.sh):

| Stage | How | Seeded defect it must catch |
|---|---|---|
| AddressSanitizer | `gcc-asan` and `clang-asan` presets; every test must pass | a heap buffer overflow |
| UndefinedBehaviorSanitizer | `gcc-ubsan` and `clang-ubsan` presets, stopping at the first undefined behaviour | a signed integer overflow |
| ThreadSanitizer | `gcc-tsan` and `clang-tsan` presets | a data race |
| libFuzzer | `clang-fuzz` preset (libFuzzer with ASan and UBSan): one minute per fuzz target per change, and an hour per target nightly ([`fuzz-nightly.yml`](../.github/workflows/fuzz-nightly.yml)) | a heap overflow behind a four-byte magic prefix, which must be found within two minutes |
| Coverage (ICS-015) | `clang-coverage` preset (Clang source-based coverage): every line and branch of each path in [`policy/coverage-gates.txt`](policy/coverage-gates.txt) covered, and its assertion density at or above its floor | a branch no test takes, and a function with no `ics::check` under a floor of 1 |

```sh
cpp/policy/check-dynamic.sh sanitizers
cpp/policy/check-dynamic.sh fuzz 60        # seconds per fuzz target
cpp/policy/check-dynamic.sh coverage
```

The assertion density of a gated path is the number of `ics::check` calls in its functions longer than three lines, divided by the number of those functions. Power of Ten rule 5 asks for two per function; container code, with one failure mode per operation, sits near 0.5, and logging and config, where bad input is an expected outcome rather than a bug, sit lower. Frames code sits at 0: its inputs are checked once, where they are made, and any further check could never fail, so its failure branch could never be covered. So each path has a floor that only rises: CI fails below it, and asks for the floor to be raised when the density rises above it. [`.github/scripts/cpp_coverage.py`](../.github/scripts/cpp_coverage.py) reads function extents from `llvm-cov export`, so a template member counts only once some test instantiates it. Add a library to the gates file when it is written, at its measured density. Test and fuzz folders are never gated. Comparisons in gated code are written out rather than defaulted, because Clang 17's coverage miscounts the branches of a defaulted comparison.

The seeded defects live in [`policy/seeded-runtime/`](policy/seeded-runtime), each naming on its first line the report it must produce.

The `asan`, `tsan` and `fuzz` presets link dependencies built with the same sanitizer, from the [`asan`](conan/profiles/asan) and [`tsan`](conan/profiles/tsan) Conan profiles: protobuf and abseil annotate their containers for ASan only when they are built with it, and TSan cannot see synchronization in uninstrumented code. Mixing instrumented and uninstrumented code gives false reports. The `ubsan` presets use the plain debug dependencies, since UBSan checks only the code it instruments. A build folder configured before this change keeps its old toolchain file; configure it again with `cmake --preset <name> --fresh`.

Add a fuzz target with `ics_add_fuzzer(name SOURCES … LIBRARIES … CORPUS folder)` from [`cmake/Fuzzing.cmake`](cmake/Fuzzing.cmake), with a few small inputs in the corpus folder; see [`toolchain_check/fuzz`](toolchain_check/fuzz). Every build compiles fuzz sources, so the warnings, clang-tidy and cppcheck cover them; the `clang-fuzz` preset links them with libFuzzer. A nightly crash fails the run, and the crashing input is uploaded as the `fuzz-crashes` artifact; reproduce it with the fuzz target and the input file as its only argument.

CodeQL analyzes the C++ code too, with the Python and TypeScript code; see [`.github/workflows/codeql.yml`](../.github/workflows/codeql.yml).

The SITL rig that the MAVLink adapter ([ICS-021](https://github.com/MatthewK84/ICS/issues/21)) is tested against is in [`deploy/sitl`](../deploy/sitl/README.md) ([ICS-018](https://github.com/MatthewK84/ICS/issues/18)).

Next issue: [ICS-019](https://github.com/MatthewK84/ICS/issues/19) (ics-timingd).
