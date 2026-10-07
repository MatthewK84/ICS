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

Builds are reproducible: `deploy/toolchain/check-reproducible.sh gcc` (or `clang`) builds the `toolchain_check` sample twice, in two build folders, and requires byte-identical outputs. The first folder also builds and tests all the ICS code in release; the second builds only the sample. The flags that make this work are in [`cmake/Reproducible.cmake`](cmake/Reproducible.cmake).

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
| [`utm.hpp`](frames/include/ics/frames/utm.hpp) | `from_utm`: a WGS84 UTM position to a geodetic point (ICS-024), with Karney's sixth-order Krüger series as in GeographicLib's `TransverseMercator`, good to nanometres. It refuses a zone outside 1 to 60 and a position beyond UTM's limits |
| [`wgs84.hpp`](frames/include/ics/frames/wgs84.hpp) | The ellipsoid's constants |

- **Real-time use:** the conversions and geoid lookups are `noexcept`, allocate nothing (the tests run them inside `NoAllocationScope`) and are safe from several threads. Load the 19 MB grid once at start-up.
- **Ported code:** the angle reductions, the inverse conversion, the UTM series and the geoid interpolation are ported from GeographicLib 2.3, under its MIT License ([`frames/GEOGRAPHICLIB-LICENSE.txt`](frames/GEOGRAPHICLIB-LICENSE.txt)). The interpolation is GeographicLib's 12-point cubic fit, with its stencil tables copied unchanged. Bilinear interpolation would be off by up to 0.14 m, against the 1 mm the golden vectors require.
- **Tests:** the frames tests read the golden CSV files and the installed grid, at paths CMake passes in (`ICS_GEOID_DIR`, which defaults to the images' `/usr/share/GeographicLib/geoids`). They fail rather than skip when the grid is missing.

## Timing

[`services/timingd/`](services/timingd) is `ics-timingd` (ICS-019), built on `ics::timing` in [`timing/`](timing). It reads the station's PTP state from `ptp4l` and publishes `ics.v1.TimeQuality` reports to the processes on the station that need them.

Each poll interval, `ics-timingd` sends GET requests for four data sets (current, parent, time properties and port) to `ptp4l`'s read-only management socket (`uds_ro_address`), and matches the answers to its requests by sequence number. [`ptp_management.hpp`](timing/include/ics/timing/ptp_management.hpp) encodes the requests and decodes the answers (IEEE 1588-2008 clause 15). The decoder's tests and its fuzz target's seed corpus are answers captured from `ptp4l` 4.0.

| Clock state | When |
|---|---|
| `locked` | The port is `SLAVE` and the grandmaster announces clock class 6, time traceable |
| `holdover` | The port is `SLAVE` and the grandmaster announces class 7, or one of the ITU-T G.8275 holdover classes (135, 140 to 160) |
| `free_running` | Anything else, including a `ptp4l` that does not answer within the poll interval |

The report's `error_bound_ns` is a model, not a measurement: |offset| plus the accuracy the grandmaster announces (IEEE 1588-2019 Table 5) plus `asymmetry_bound_ns` when locked; the same plus `holdover_drift_ns_per_s` times the time in holdover; and unbounded, written as INT64_MAX, when free-running. Holdover is timed from the first poll that shows it. PTP does not carry `gnss_satellite_count` or `irig_b_locked`, so they stay 0 and false.

- **Reports.** The build plan has `ics-timingd` serve reports as a gRPC server stream. gRPC is not in the toolchain yet, because Conan Center's gRPC package needs an older protobuf than the 7.35.0 the lockfile pins. Until it is, [`publisher.hpp`](timing/include/ics/timing/publisher.hpp) sends each report to every subscriber on a local `SOCK_SEQPACKET` socket (`publish_socket`), one serialized `TimeQuality` per message, which is a server stream's shape. Up to 16 subscribers; one that cannot take a report at once is dropped and must reconnect.
- **Camera offsets.** `camera_offsets` comes from the camera offsets file the strobe analyzer writes ([Strobe calibration](#strobe-calibration)), named by `camera_offsets_file`. [`camera_offsets.hpp`](timing/include/ics/timing/camera_offsets.hpp) reads and writes it: a serialized `TimeQuality` with only `station_id` and `camera_offsets` set, at most 1 MiB, written to a temporary file, synced and renamed over the old one, so a reader never sees half a file.
  - Each poll checks the file's modification time, which allocates nothing. Only when it changes is the file read again, and its offsets copied into every report until it next changes.
  - A missing file means no camera is calibrated yet, and the reports carry no offsets.
  - A file that is malformed, or another station's, is logged and used for nothing.
  - An offset stays until the next calibration replaces it; its `measured_utc_ns` says how old it is.
- **Logs.**
  - `started`, `start_failed`, and `stopped`, with the `signal`.
  - `clock_state` when the state changes, with `state`, `ptp_offset_ns` and `error_bound_ns`.
  - `ptp4l_answering` when `ptp4l` starts answering, and as a warning, `ptp4l_unavailable` when it stops.
  - When the camera offsets file changes: `camera_offsets` with their `count`; `camera_offsets_none` for no file; or as a warning, `camera_offsets_unusable` with the `error`.
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
- **Time.** Each `SYSTEM_TIME` that carries UTC gives the vehicle's boot-to-UTC offset, and a record is valid at its `time_boot_ms` plus the last offset (`PLI_TIME_BASIS_VEHICLE_GNSS`). Before a vehicle sends UTC, its records take the time they were received (`PLI_TIME_BASIS_RECEIPT`), the capture's time stamp. The adapter keeps no drift fit: it passes each `SYSTEM_TIME` that carries UTC on in `Output::clocks`, and after a sortie `ics::timealign` fits them all at once ([Time alignment](#time-alignment)).
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

## Lattice

[`lattice/`](lattice) is `ics::lattice` (ICS-023), the third PLI adapter. It streams entities from Anduril's Lattice over its REST API, `POST /api/v1/entities/stream`, read as server-sent events, and turns each entity's position into an `ics.v1.PliRecord` ([`pli.proto`](../proto/ics/v1/pli.proto)). It is written against the public REST API with libcurl; Anduril's SDKs are not used, since their license forbids redistributing or modifying them.

| Header | Provides |
|---|---|
| [`stream_client.hpp`](lattice/include/ics/lattice/stream_client.hpp) | `StreamClient`: the stream over HTTPS, connecting again after errors and silences, and stopping on a refused request |
| [`sse.hpp`](lattice/include/ics/lattice/sse.hpp) | `SseReader`: server-sent events, read from chunks of any size, with a limit on an event's length |
| [`event.hpp`](lattice/include/ics/lattice/event.hpp) | `decode_event`: an event's JSON, read with protobuf's JSON parser into a `google.protobuf.Struct`, then field by field |
| [`adapter.hpp`](lattice/include/ics/lattice/adapter.hpp) | `Adapter`: a record from each current position of a live entity |
| [`token.hpp`](lattice/include/ics/lattice/token.hpp) | `read_token`: a token from a file only its owner can reach |

| Lattice | `PliRecord` |
|---|---|
| `entity.entityId` | `entity_id`; the role comes from the settings, `ENTITY_ROLE_OTHER` for an entity not listed |
| `location.position`: `latitudeDegrees`, `longitudeDegrees`, `altitudeHaeMeters` | `position`; with no `altitudeHaeMeters` the height is 0 and not valid |
| `location.velocityEnu`: `e`, `n`, `u` at the entity | `velocity_enu_mps`, rotated into the range ENU frame |
| `locationUncertainty.positionEnuCov` | `horizontal_sigma_m`: the square root of the larger eigenvalue of its east-north block; `vertical_sigma_m`: the square root of `mzz`, when the height is known |
| `provenance.sourceUpdateTime` | `valid_utc_ns` with `PLI_TIME_BASIS_VEHICLE_GNSS` when within `max_skew` (30 s) of receipt, as for CoT; otherwise the receipt time with `PLI_TIME_BASIS_RECEIPT` |
| (none) | `fix_type`: `FIX_TYPE_OTHER`, since Lattice reports no GNSS fix |

- **Events.** `PREEXISTING`, `CREATED` and `UPDATE` events of a live entity with a position give a record; a deleted or expired entity, or one without a position, gives nothing, and staleness is left to `ics-plid` (ICS-030). Lattice writes protobuf messages as JSON, which leaves out zeros, so a missing number inside a position, vector or matrix is 0. Times are RFC 3339 from 1970 to 2200, read with the CoT time parser: protobuf's own `TimeUtil::FromString` aborts in a debug build on a year past 9999, which fuzzing found.
- **Connection.** Each request asks for every existing entity and then every change, with a heartbeat every 5 s. A failed or ended connection, a 408, 429 or 5xx answer, or 15 s without a byte means connecting again after a pause that doubles from 1 s to 30 s; every new connection sends every existing entity again. Any other 4xx, such as 401 or 403 for a refused token, ends the run with an error.
- **Security.** HTTPS with the server's certificate and name verified; plain HTTP only to this host, for tests. Redirects are not followed. The token is read from a file that only its owner may reach, and must be printable ASCII without spaces, so it cannot add a header; it is never logged. The client only reads the stream; the token's read-only scope is set in Lattice. Sandboxes also take `Anduril-Sandbox-Authorization`.
- **Probe.** [`testing/lattice_probe`](testing/lattice_probe/main.cpp) is `ics-lattice-probe`, which streams for a while and prints the records as JSON lines. The [Lattice sandbox workflow](../.github/workflows/lattice-sandbox.yml) runs it nightly against a Lattice Sandbox once the `LATTICE_URL`, `LATTICE_ENVIRONMENT_TOKEN` and `LATTICE_SANDBOX_TOKEN` secrets are set.
- **Tests.** A synthetic [sample stream](lattice/test/samples/README.md), read in chunks of every size from 1 to 64 bytes, and a loopback HTTP server that plays back streams, errors and silences to the client. The fuzz target reads its input as a stream and as an event.

## SAPIENT

[`sapient/`](sapient) is `ics::sapient` (ICS-024), the fourth PLI adapter. It reads the BSI Flex 335 v2.0 messages a SAPIENT node sends, and turns the positions in their detection reports into `ics.v1.PliRecord`. The messages are Dstl's protobuf definitions, copied unchanged into [`proto/third_party/sapient_msg`](../proto/third_party/README.md) and generated into `ics::proto`. The adapter does no network I/O; ICS-030 decides how `ics-plid` connects to nodes.

| Header | Provides |
|---|---|
| [`stream.hpp`](sapient/include/ics/sapient/stream.hpp) | `StreamReader`: the messages in a node's TCP stream, each a 4-byte little-endian length and then the message, as Dstl's test harness frames them. A length over 1 MiB, or a message that does not parse, breaks the stream, which then reads nothing more |
| [`location.hpp`](sapient/include/ics/sapient/location.hpp) | `to_fix`: a SAPIENT location as a WGS84 point with one-sigma errors in metres; `parse_zone`: a UTM zone such as `30U` |
| [`registration.hpp`](sapient/include/ics/sapient/registration.hpp) | `read_units`: the UTM zone and velocity units a node registers |
| [`adapter.hpp`](sapient/include/ics/sapient/adapter.hpp) | `Adapter`: each node's units from its registration, and a record from each detection report with a location |

| SAPIENT | `PliRecord` |
|---|---|
| `detection_report.object_id` | `entity_id`; the role comes from the settings, matched to the `object_id` or the report's `id` (such as a tail number), and is `ENTITY_ROLE_OTHER` for an object not listed |
| `location`: `x`, `y`, `z` | `position`: x is the longitude or easting and y the latitude or northing, in degrees, radians or UTM metres. UTM uses the location's `utm_zone`, or else the node's registered zone. z is the ellipsoid height for `WGS84_E`, and the height above mean sea level, through EGM96 ([Frames](#frames)), for `WGS84_G`; without z the height is 0 and not valid |
| `location`: `x_error`, `y_error`, `z_error` | `horizontal_sigma_m`: the larger of the x and y errors in metres; `vertical_sigma_m`: `z_error`, when the height is known |
| `enu_velocity` at the object | `velocity_enu_mps`, in the node's registered units (m/s or km/h), rotated into the range ENU frame; a missing up rate is 0. Unset until the node registers |
| `timestamp` | `valid_utc_ns` with `PLI_TIME_BASIS_VEHICLE_GNSS` when within `max_skew` (30 s) of receipt, as for CoT; otherwise the receipt time with `PLI_TIME_BASIS_RECEIPT` |
| (none) | `fix_type`: `FIX_TYPE_OTHER`, since SAPIENT reports no GNSS fix |

- **Zones.** A zone is its number and a latitude band letter, as in Dstl's samples (`30U`). C to M are south of the equator and N to X north. A UTM position must lie in its band, give or take 1 degree, except in band N, which is read as the northern hemisphere. A sender that writes `56S` for zone 56 south would otherwise put Sydney at 56 N; the band check refuses it instead. It cannot catch such a sender between about 49 S and 59 S, whose points fall in band S when read as northern.
- **Registrations.** A detection does not name the mode it was made in, so a zone or velocity unit counts only when every detection definition in the registration that gives one agrees. Units are kept for up to `max_nodes` (1024) nodes, keyed by `node_id`. A node that registered before the adapter started has no units until it registers again.
- **Skipped and counted.** Detection reports with a range and bearing, which ICS does not convert; with no `object_id`; or with no location, or one that cannot be placed. Status reports, tasks, alerts and the other messages give nothing.
- **Tests.** Dstl's 83 sample messages and a synthetic session ([samples](sapient/test/samples/README.md)), framed and read back in chunks of every size from 1 to 64 bytes. The fuzz target reads its input as a framed stream and as one message.

## Onboard logs

[`flightlog/`](flightlog) is `ics::flightlog` (ICS-025). It imports the logs an autopilot writes on board, PX4's ULog and ArduPilot's DataFlash, and turns their estimator states, raw GNSS fixes and events into `ics.v1.PliRecord` and `PliEvent`, every sample at the rate the log holds it. After a test, they fill gaps in what the PLI adapters recorded live, and the drift fit (ICS-026) aligns their boot clocks.

| Header | Provides |
|---|---|
| [`ulog.hpp`](flightlog/include/ics/flightlog/ulog.hpp) | `ULog`: a ULog log's formats, each topic instance's samples, parameters, information and logged strings, with data appended after a crash. A malformed or truncated message ends a section, keeping what came before |
| [`dataflash.hpp`](flightlog/include/ics/flightlog/dataflash.hpp) | `DataFlash`: a DataFlash log's formats (FMT) and each type's messages; bytes that start no message are skipped and counted, so a damaged log keeps the messages around the damage |
| [`gps_time.hpp`](flightlog/include/ics/flightlog/gps_time.hpp) | `utc_from_gps`: UTC from a GPS week and milliseconds, through a leap-second table ([Time](../docs/frames-and-time.md#time)) |
| [`import.hpp`](flightlog/include/ics/flightlog/import.hpp) | `import_log`: either log as records and events, each with the boot time the log gave it |

| Log | States | GNSS fixes | Events |
|---|---|---|---|
| PX4 (ULog) | `vehicle_global_position`, with the latest `vehicle_local_position` velocity and `vehicle_attitude` no older than `max_age` (1 s); `eph` and `epv` are the sigmas | `vehicle_gps_position`, or `sensor_gps`, of fix type 2D or better: degrees and metres since PX4 v1.14, 1e-7 degrees and millimetres before | `vehicle_status` arming and navigation state changes, named as v1.17 names them; logged strings |
| ArduPilot (DataFlash) | `POS`, with EKF3 core 0's velocity (`XKF1`, or EKF2's `NKF1`) and `ATT` | The first receiver's `GPS` messages of status 2D or better, with the matching `GPA` accuracies | `MODE`, named as ArduCopter names its modes when the log is ArduCopter's; `ARM`; `MSG` text |

- **Time.** Each GNSS sample with a time ties the boot clock to UTC: PX4's `time_utc_usec`, the receiver's own UTC, at `timestamp` + `timestamp_time_relative`; ArduPilot's GPS week and milliseconds. A record or event takes the latest such offset at or before it, or the first one, with `PLI_TIME_BASIS_VEHICLE_GNSS`. A log with no GNSS time, such as one from PX4's SIH simulator, is untimed: `PLI_TIME_BASIS_UNSPECIFIED`, `valid_utc_ns` 0, and only the boot times, for ICS-026 to align. GNSS times from 2100 on, and boot times past 2100 as a Unix time, are refused.
- **Identity.** `entity_id` is the MAVLink system ID the log's parameters give (`MAV_SYS_ID`; `MAV_SYSID`, or `SYSID_THISMAV` before ArduPilot 4.7), so it matches the MAVLink adapter's; 1 if none does. The role comes from the settings, by system ID.
- **Left out and counted.** Positions not valid or not on the globe, fixes without a 2D fix, and samples too short or with a boot time ICS does not take. A velocity or attitude that is not finite is left off its record, as is a sigma that is not a finite length.
- **Import tool.** [`testing/log_import`](testing/log_import/main.cpp) is `ics-log-import LOG LATITUDE LONGITUDE HEIGHT [SYSTEM=ROLE]...`, which imports a log and writes JSON lines, as `ics-mavlink-replay` does. The SITL rig saves both autopilots' logs ([`deploy/sitl`](../deploy/sitl/README.md)).
- **Tests.** The readers find exactly what pyulog and pymavlink find in five sample logs, two from the SITL rig and three from pyulog ([logs](flightlog/test/logs/README.md)), and every estimated position and fix they count becomes a record. Synthetic logs cover every other case. The two fuzz targets import any input, and check that every record and event keeps the importer's promises.

## Time alignment

[`timealign/`](timealign) is `ics::timealign` (ICS-026, CPP-11). After a sortie, it fits each vehicle's boot clock to UTC from the clock pairs the MAVLink adapter and the log importer pass on, and re-times the sortie's records with the fit ([Frames and time](../docs/frames-and-time.md#vehicle-boot-clocks)).

| Header | Provides |
|---|---|
| [`clock_fit.hpp`](timealign/include/ics/timealign/clock_fit.hpp) | `fit_clock`: one sortie's offset and drift by least squares, outliers left out; `ClockModel`: UTC at a boot time; `straight`: whether the pairs lie within 0.5 ms RMS of the line |
| [`sortie.hpp`](timealign/include/ics/timealign/sortie.hpp) | `SortieCounter`: which boot of a vehicle a boot time belongs to; a boot time more than 1 s back is a reboot |
| [`align.hpp`](timealign/include/ics/timealign/align.hpp) | `aligned`: a `PliRecord` or `PliEvent` re-timed by a model, as `PLI_TIME_BASIS_VEHICLE_ALIGNED`; `SteppedClock` and `stepped`: timed by the latest clock pair at or before, as the live adapters time them, `PLI_TIME_BASIS_VEHICLE_GNSS` |
| [`latency.hpp`](timealign/include/ics/timealign/latency.hpp) | `summarize_latency`: count, minimum, median, 95th percentile and maximum of the time received minus the aligned time |
| [`check.hpp`](timealign/include/ics/timealign/check.hpp) | `check_injected_drift` and `judge`: the "Done when" check, below |

- **Clock pairs.** `mavlink::Output::clocks` holds each `SYSTEM_TIME` that carries UTC, and `flightlog::LogContents::gnss_times` each GNSS time in a log. The adapter's live timing is unchanged.
- **Straight clocks only.** A sortie whose pairs are not within 0.5 ms RMS of their line keeps the live times. PX4 SIH's are not, on the rig: its boot clock is simulated time, and uneven.
- **The check.** `check_injected_drift` puts a known drift and offset on a sortie's boot times, withholds part of its pairs as a GNSS outage would, fits what is left, and compares each position's time with its reference. The reference is the position's boot time plus the mean offset of the pairs as sent, since SITL's clocks should have no drift of their own. `judge` passes a sortie within 1 ms (`kAlignmentLimit`). It applies only to a sortie whose offsets stay within 0.5 ms of their mean (`kMaxReferenceSpread`), and reports any other as `kDrifting`: PX4 SIH's drift by about 2.5 %.
- **Tool.** [`testing/time_align`](testing/time_align/main.cpp) is `ics-time-align PCAP LATITUDE LONGITUDE HEIGHT [--log FILE]... [--inject PPM:OFFSET_MS [--withhold FROM:TO]] [--records]`. It replays a TAP capture through the MAVLink adapter, fits each vehicle's sorties, reports each vehicle's latency, times onboard logs, and with `--inject` runs the check and exits 1 unless it passes. Every night the [SITL rig workflow](../.github/workflows/sitl.yml) runs it on each engagement ([`deploy/sitl`](../deploy/sitl/README.md#time-alignment)).
- **Onboard logs.** A log is timed from the `SYSTEM_TIME` pairs of the sortie its boot times overlap, on the same clock as the live records: by the sortie's fit when straight, otherwise by the latest pair at or before each record (`SteppedClock`). Its own GNSS times are only compared with them, never fitted with them. On the `crossing` engagement ArduCopter's log GNSS times sit 36.6 ms before its `SYSTEM_TIME` pairs, most likely the lag from a fix to its logging. A log with no matching sortie keeps the importer's times, and PX4 SIH's untimed log gets the live clock's.
- **On SITL.** On a 150 s `crossing` engagement, ArduCopter's clock drifts −0.16 ppm and its pairs are 0.13 ms RMS from the line. With 100 ppm injected either way and the middle 60 % withheld, every position aligns within 21 µs. Its positions arrive 0.78 s to 0.80 s after their aligned times: its simulated GNSS time is that far behind the host's clock. PX4 SIH's offsets spread over 1.9 s, so its sortie is not checked. On CI's runners, across the three engagements, ArduCopter aligns within 41 µs and PX4 SIH's pairs are 0.8 ms to 1.1 ms RMS from their line.
- **Tests.** Synthetic clocks cover every case, and [`test/fixtures`](timealign/test/fixtures/README.md) holds that engagement's clock pairs and position times, as the rig's own decoder read them, for the check. The fuzz target fits any pairs and checks that each fit and check keeps its promises.

## Cameras

[`camera/`](camera) is `ics::camera` (ICS-027 and ICS-028, CPP-12 and CPP-13). It records segments on the high-speed cameras, a Phantom (visible) and a FLIR X6980-HS (mid-wave infrared). It saves each segment as a cine file and checks every saved segment's frame times. ICS does not have either camera's SDK yet, so emulated cameras stand in for both. Under the Risk Management Framework, ICS is built in its entirety before any test on real hardware. The SDK bindings and runs on real cameras are in the backlog ([#140](https://github.com/MatthewK84/ICS/issues/140) for the Phantom).

| Header | Provides |
|---|---|
| [`cine.hpp`](camera/include/ics/camera/cine.hpp) | `read_cine`: a cine's frame numbers, trigger time, image size, frame rate and exposure, each frame's time and exposure, and where its images lie, all checked against the bytes; `write_cine`: a cine of the times and images given, or of images of zeros; `from_time64` and `to_time64` |
| [`mapped_file.hpp`](camera/include/ics/camera/mapped_file.hpp) | `MappedFile`: a saved cine mapped read-only |
| [`segment_camera.hpp`](camera/include/ics/camera/segment_camera.hpp) | `SegmentCamera`: configure (window, frame rate, exposure, segments), arm segments, trigger, and save frames as a cine; `TimeQualitySource`: the station's time quality |
| [`emulated_phantom.hpp`](camera/include/ics/camera/emulated_phantom.hpp) | `EmulatedPhantom`: a Phantom in software whose clock follows IRIG-B exactly, triggered on a schedule (`trigger_schedule.hpp`) |
| [`irig.hpp`](camera/include/ics/camera/irig.hpp) | `IrigStamp` and `utc_from_irig`: an IRIG-B time stamp to UTC, with or without its year ([Camera clocks](../docs/frames-and-time.md#camera-clocks)) |
| [`flir_sdk.hpp`](camera/include/ics/camera/flir_sdk.hpp) | `FlirSdk`: what ICS needs from the X6980's SDK, frame by frame, each frame with its IRIG-B stamp and pixels |
| [`emulated_x6980.hpp`](camera/include/ics/camera/emulated_x6980.hpp) | `EmulatedX6980`: the X6980's SDK in software, with a 640 × 512 sensor and microsecond IRIG-B stamps, with or without the year |
| [`flir_camera.hpp`](camera/include/ics/camera/flir_camera.hpp) | `FlirCamera`: the X6980 as a `SegmentCamera`, converting each stamp to UTC and writing the frames as a cine |
| [`strobe.hpp`](camera/include/ics/camera/strobe.hpp) | `StrobeSchedule` and `strobe_light`: the PPS strobe's pulses and the light an exposure holds; `StrobeScene`: what an emulated camera sees of it (ICS-029) |
| [`image.hpp`](camera/include/ics/camera/image.hpp) | `roi_mean`: the mean pixel value in a rectangle of a frame's image, from unpacked 8- or 16-bit pixels (ICS-029) |
| [`frame_meta.hpp`](camera/include/ics/camera/frame_meta.hpp) | `TimeAuthority` and `frame_meta`: a `CameraFrameMeta` for each frame of a cine, with its camera kind and sensor window offset |
| [`offload.hpp`](camera/include/ics/camera/offload.hpp) | `record_and_offload` and `verify_segment`: the "Done when" sequence and check, below |

- **The format.** ICS reads a cine as the PIMS project's reader lays it out ([soft-matter/pims](https://github.com/soft-matter/pims) `cine.py`, BSD-3-Clause). PIMS is a reference only, and no PIMS code is used; Vision Research's own format document could not be fetched from the build environment. PIMS 0.7 reads ICS's sample cine exactly as `read_cine` does ([fixtures](camera/test/fixtures/README.md)). A frame's time is tagged block 1002, a TIME64 ([Camera clocks](../docs/frames-and-time.md#camera-clocks)). A cine without frame times is refused.
- **The X6980's segments are cines too.** FLIR's own recording format (`.ats`) is undocumented, and the only open reader of it ([pyFlir](https://pypi.org/project/pyflir/), MIT) finds frames by a sync marker and reads no times. So `FlirCamera` reads each frame from the SDK with its IRIG-B stamp and pixels, converts the stamp to UTC, and writes the segment with `write_cine`: 16-bit pixels, 14 bits of them significant. The rest of the sequence, the verification and ICS-062's decoding are the same for both cameras. The setup inside the file names Phantom software version 702, and the camera kind is in each frame's `CameraFrameMeta`.
- **Timed by IRIG.** Whether a frame's time is IRIG time comes from what ICS knows, not from the file: the settings the camera reports it applied (`CameraSettings::irig`), and `TimeQuality.irig_b_locked` when the segment was recorded. Frames are `TIME_SOURCE_IRIG` only when both say so, and `TIME_SOURCE_HOST` otherwise.
- **Sensor window.** `CameraSettings::window_x` and `window_y` place the window on the sensor. Each frame's `CameraFrameMeta.window_x_px` and `window_y_px` record them as the camera applied them, for registration. The emulated Phantom applies 0 and 0 whatever is asked.
- **The sequence.** `record_and_offload` configures the camera, arms it for the plan's segments, and triggers each in turn. It then saves the plan's frames of each segment as `cine-NNN.cine`, maps the file, reads it and verifies it. The plan names the camera kind.
- **Verification.** A segment verifies when all of these hold:
  - it has the planned number of frames, starting at the planned frame number;
  - its frames are IRIG-timed;
  - consecutive frames are one frame period apart, within 1 µs (`kSpacingTolerance`);
  - its trigger is within one frame period of the time the camera reported, both in the cine's header and where the first frame's time and number put it;
  - it starts after the previous segment ends.
- **Done when.** Ten back-to-back segments offload with verified times on each emulated camera, 300 frames of each 500-frame segment saved, 400 of them after the trigger:
  - the Phantom at 5,000 frames/s, with triggers 120 ms apart;
  - the X6980 at 1,004 frames/s in a 64 × 32 window at (288, 240), with triggers 600 ms apart and stamps without the year. Its microsecond stamps put consecutive frames 996 µs or 997 µs apart, at most 984 ns from the 996,015.9 ns period, within the 1 µs tolerance.

  Segments not timed by IRIG do not verify.
- **Bench tool.** [`testing/camera_bench`](testing/camera_bench/main.cpp) is `ics-camera-bench phantom|x6980`. It runs that sequence on an emulated camera and saves the cines in the working directory, which is not an argument because CodeQL's path-injection rule rejects a path taken from the command line. It prints a JSON line for each segment and one for the verdict, and exits 1 unless every segment verifies. It is registered as a test for each camera, so CI runs both in every sanitizer build.
- **Tests.** Tests cover:
  - every refusal of the reader and writer;
  - IRIG-B conversion: New Year, leap days and fields out of range;
  - the emulators, the sequence and the check on both emulated cameras;
  - a camera, and an X6980 SDK, that fails or hands over bad frames at each step.

  The fuzz target reads any bytes. It checks that each cine it reads keeps the reader's promises, and that a cine the writer can write, with images up to 16 MiB, reads back unchanged.

## Strobe calibration

[`strobe/`](strobe) is `ics::strobe` (ICS-029, CPP-04). It measures each camera's time offset, its frame time stamp less the true start of the frame's exposure, from the PPS strobe the camera recorded. It publishes the offset through `ics-timingd`'s camera offsets file. `frame_meta` (ICS-027, ICS-028) subtracts the latest offset from every frame time, so the calibration corrects every frame downstream ([Camera clocks](../docs/frames-and-time.md#camera-clocks)).

| Header | Provides |
|---|---|
| [`detect.hpp`](strobe/include/ics/strobe/detect.hpp) | `read_segment`: each frame's mean brightness in the strobe's rectangle (`camera::roi_mean`), and which frames the strobe lit |
| [`fit.hpp`](strobe/include/ics/strobe/fit.hpp) | `fit_offset`: the offset that best explains the frames, with its one-sigma uncertainty |
| [`analyze.hpp`](strobe/include/ics/strobe/analyze.hpp) | `analyze_folder`: a calibration's cines to a `TimeQuality.CameraOffset`; `publish`: the offset into the station's camera offsets file |
| [`config.hpp`](strobe/include/ics/strobe/config.hpp), [`run.hpp`](strobe/include/ics/strobe/run.hpp) | `ics-strobe-analyzer`'s settings and command |

- **The strobe.** An LED lights for `pulse_width` at each UTC second, after its own `latency` and a sweep delay. A delay generator steps the sweep delay by `delay_step` each second, through `sweep_steps` steps (`camera::StrobeSchedule`). The camera records one segment around each PPS, saved as a cine.
- **Detection.** In each segment, a frame is lit when its rectangle's mean brightness exceeds the segment's median by six robust sigmas (1.4826 median absolute deviations), and by at least one count. Only segments with consecutive lit frames are fitted. Segments with no lit frame are left out: the pulse fell between exposures, or the strobe missed a second. So are segments whose lit frames are apart, as from stray light. Each is counted.
- **The fit.** A frame's brightness is modeled as gain × (strobe light within its exposure) + background. The exposure is taken to start at the frame's stamp less the offset, so the fit searches offsets within `max_offset` of 0: on a 250 ns grid, then to the nanosecond, solving the gain and background by least squares at each.
  - The sigma is the residuals' RMS over the information the frames carry about the offset, once gain and background are fitted too.
  - The fit fails with `Error::kUnconstrained` when the frames carry no such information, when the best offset lies at the search's edge, or when a full pulse's brightness is under ten times the residuals' RMS.
  - With no sweep, every lit frame holds the same light, so a larger gain times less of the pulse fits as well as the true one, and the fit fails rather than guessing.
  - A delay step shorter than the pulse makes a pulse straddle every exposure edge the sweep crosses.
- **Tool.** [`ics-strobe-analyzer`](strobe/tool/main.cpp) takes no arguments.
  - It reads `/etc/ics/ics-strobe.toml`, a config like [`ics-strobe.toml`](strobe/ics-strobe.toml), and analyzes every `*.cine` in the working folder.
  - It replaces its camera's offset in the offsets file, keeping the other cameras', and logs `camera_offset` with the offset, its sigma and the segments behind it.
  - It exits 0 when the offset is published, 1 when the cines could not be analyzed or the offset published, and 2 for an argument or a bad config file.
- **Done when.** Offsets within 5 µs on the bench. Under RMF, ICS is built in its entirety before any test on real hardware, so the bench is emulated for now, as for ICS-027 and ICS-028.
  - The emulated cameras render the strobe's light into their images, with Gaussian noise, and stamp each frame late by a known offset (`camera::StrobeScene`).
  - [`testing/strobe_bench`](testing/strobe_bench/main.cpp) is `ics-strobe-bench phantom|x6980`. It records 40 segments, measures the offset, publishes it and reads it back through the offsets file. It is a test for each camera in every preset.
  - The Phantom (5,000 frames/s, 150 µs exposures, a 20 µs pulse swept by 5 µs) is stamped 37.4 µs late, and measures 37.404 µs with a sigma of 26 ns.
  - The X6980 (1,004 frames/s, 800 µs, a 30 µs pulse swept by 25 µs) is stamped 112.6 µs early, and measures −113.067 µs with a sigma of 51 ns. Its stamps, truncated to the microsecond, read 0.5 µs early on average, and that is part of its offset.
- **Tests.**
  - The fit, on synthetic frames: offsets either side of 0, and each way it refuses.
  - Detection on cines written for it.
  - The analysis, `publish`, the settings and the command, on the emulated cameras.
  - The offsets file's fuzz target reads any bytes, and checks that what it accepts keeps its promises and reads back unchanged.

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
cpp/policy/check-policy.sh          # everything
cpp/policy/check-policy.sh gcc      # the GCC build and its seeds
cpp/policy/check-policy.sh clang    # the Clang build, clang-tidy, cppcheck and their seeds
```

CI runs the `gcc` and `clang` halves side by side; both check for suppressions. The script also proves each rule still fires: every file in [`policy/seeded/`](policy/seeded) breaks exactly one rule and must be rejected with the diagnostic named on its first line. The seeds build only with `-DICS_POLICY_SEEDS=ON`.

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
cpp/policy/check-dynamic.sh sanitizers             # all six presets
cpp/policy/check-dynamic.sh sanitizers gcc-asan    # one preset
cpp/policy/check-dynamic.sh fuzz 60                # seconds per fuzz target
cpp/policy/check-dynamic.sh coverage
```

The fuzz stage builds only the fuzz targets and runs as many at once as there are processors, so each still gets its full time. In CI, the [C++ toolchain workflow](../.github/workflows/cpp-toolchain.yml) runs every stage as its own job, side by side: the two reproducible builds, the six sanitizer presets, fuzzing, coverage and the two policy halves. With the Conan cache warm, a change takes under ten minutes.

The assertion density of a gated path is the number of `ics::check` calls in its functions longer than three lines, divided by the number of those functions. Power of Ten rule 5 asks for two per function; container code, with one failure mode per operation, sits near 0.5, and logging and config, where bad input is an expected outcome rather than a bug, sit lower. Frames code sits at 0: its inputs are checked once, where they are made, and any further check could never fail, so its failure branch could never be covered. So each path has a floor that only rises: CI fails below it, and asks for the floor to be raised when the density rises above it. [`.github/scripts/cpp_coverage.py`](../.github/scripts/cpp_coverage.py) reads function extents from `llvm-cov export`, so a template member counts only once some test instantiates it. Add a library to the gates file when it is written, at its measured density. Test and fuzz folders are never gated. Comparisons in gated code are written out rather than defaulted, because Clang 17's coverage miscounts the branches of a defaulted comparison.

The seeded defects live in [`policy/seeded-runtime/`](policy/seeded-runtime), each naming on its first line the report it must produce.

The `asan`, `tsan` and `fuzz` presets link dependencies built with the same sanitizer, from the [`asan`](conan/profiles/asan) and [`tsan`](conan/profiles/tsan) Conan profiles: protobuf and abseil annotate their containers for ASan only when they are built with it, and TSan cannot see synchronization in uninstrumented code. Mixing instrumented and uninstrumented code gives false reports. The `ubsan` presets use the plain debug dependencies, since UBSan checks only the code it instruments. A build folder configured before this change keeps its old toolchain file; configure it again with `cmake --preset <name> --fresh`.

Add a fuzz target with `ics_add_fuzzer(name SOURCES … LIBRARIES … CORPUS folder)` from [`cmake/Fuzzing.cmake`](cmake/Fuzzing.cmake), with a few small inputs in the corpus folder; see [`toolchain_check/fuzz`](toolchain_check/fuzz). Every build compiles fuzz sources, so the warnings, clang-tidy and cppcheck cover them; the `clang-fuzz` preset links them with libFuzzer. A nightly crash fails the run, and the crashing input is uploaded as the `fuzz-crashes` artifact; reproduce it with the fuzz target and the input file as its only argument.

CodeQL analyzes the C++ code too, with the Python and TypeScript code; see [`.github/workflows/codeql.yml`](../.github/workflows/codeql.yml).

The SITL rig that the MAVLink adapter ([ICS-021](https://github.com/MatthewK84/ICS/issues/21)) is tested against is in [`deploy/sitl`](../deploy/sitl/README.md) ([ICS-018](https://github.com/MatthewK84/ICS/issues/18)).

Next issue: [ICS-019](https://github.com/MatthewK84/ICS/issues/19) (ics-timingd).
