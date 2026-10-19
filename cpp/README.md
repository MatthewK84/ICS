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
- From ICS-037 on, new C++ also follows the JSF AV C++ Coding Standards; see [JSF AV checks](#jsf-av-checks).

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

[`services/timingd/`](services/timingd) is `ics-timingd` (ICS-019), built on `ics::timing` in [`timing/`](timing). It reads the station's PTP state from `ptp4l` and serves `ics.v1.TimeQuality` reports over gRPC to the processes on the station that need them.

Each poll interval, `ics-timingd` sends GET requests for four data sets (current, parent, time properties and port) to `ptp4l`'s read-only management socket (`uds_ro_address`), and matches the answers to its requests by sequence number. [`ptp_management.hpp`](timing/include/ics/timing/ptp_management.hpp) encodes the requests and decodes the answers (IEEE 1588-2008 clause 15). The decoder's tests and its fuzz target's seed corpus are answers captured from `ptp4l` 4.0.

| Clock state | When |
|---|---|
| `locked` | The port is `SLAVE` and the grandmaster announces clock class 6, time traceable |
| `holdover` | The port is `SLAVE` and the grandmaster announces class 7, or one of the ITU-T G.8275 holdover classes (135, 140 to 160) |
| `free_running` | Anything else, including a `ptp4l` that does not answer within the poll interval |

The report's `error_bound_ns` is a model, not a measurement: |offset| plus the accuracy the grandmaster announces (IEEE 1588-2019 Table 5) plus `asymmetry_bound_ns` when locked; the same plus `holdover_drift_ns_per_s` times the time in holdover; and unbounded, written as INT64_MAX, when free-running. Holdover is timed from the first poll that shows it. PTP does not carry `gnss_satellite_count` or `irig_b_locked`, so they stay 0 and false.

- **Reports.** `ics-timingd` serves `TimeQualityService` ([`time_quality_service.proto`](../proto/ics/v1/time_quality_service.proto)) over gRPC on a Unix-domain socket at `publish_socket` (#150). `WatchTimeQuality` is a server stream: the latest report at once, then each new one. [`report_server.hpp`](services/timingd/include/ics/timingd/report_server.hpp) serves it.
  - The poll loop never waits for a subscriber. It posts each serialized report to [`report_board.hpp`](services/timingd/include/ics/timingd/report_board.hpp), which holds only the latest, and each subscriber's gRPC thread copies it out from there.
    - Posting allocates nothing; gRPC allocates on the subscribers' threads only.
    - The loop waits only while a subscriber's thread copies the previous report, which takes microseconds.
  - A subscriber that reads more slowly than the loop reports skips to the latest report. Subscribers want the current state, not its history.
  - Up to 16 subscribers, which bounds the server's memory; the next is refused with `RESOURCE_EXHAUSTED` until one leaves.
  - [`ics-time-watch [SOCKET [COUNT]]`](services/timingd/include/ics/timingd/watch_client.hpp) prints each report as it comes, one JSON line each in protobuf's JSON form, from `/run/ics-timingd/time-quality` unless SOCKET is given. With COUNT it stops after that many. The timing bench reads holdover from it.
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
- **Probe.** [`testing/lattice_probe`](testing/lattice_probe/main.cpp) is `ics-lattice-probe`, which streams for a while and prints the records as JSON lines. The [Lattice sandbox workflow](../.github/workflows/lattice-sandbox.yml) runs it by hand against a Lattice Sandbox once the `LATTICE_URL`, `LATTICE_ENVIRONMENT_TOKEN` and `LATTICE_SANDBOX_TOKEN` secrets are set.
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
- **Tool.** [`testing/time_align`](testing/time_align/main.cpp) is `ics-time-align PCAP LATITUDE LONGITUDE HEIGHT [--log FILE]... [--inject PPM:OFFSET_MS [--withhold FROM:TO]] [--records]`. It replays a TAP capture through the MAVLink adapter, fits each vehicle's sorties, reports each vehicle's latency, times onboard logs, and with `--inject` runs the check and exits 1 unless it passes. The replay and the log timing are `ics::retime` ([`retime/`](retime)), which `ics-pli-import` uses to store them (ICS-030). Every night the [SITL rig workflow](../.github/workflows/sitl.yml) runs it on each engagement ([`deploy/sitl`](../deploy/sitl/README.md#time-alignment)).
- **Onboard logs.** A log is timed from the `SYSTEM_TIME` pairs of the sortie its boot times overlap, on the same clock as the live records: by the sortie's fit when straight, otherwise by the latest pair at or before each record (`SteppedClock`). Its own GNSS times are only compared with them, never fitted with them. On the `crossing` engagement ArduCopter's log GNSS times sit 36.6 ms before its `SYSTEM_TIME` pairs, most likely the lag from a fix to its logging. A log with no matching sortie keeps the importer's times, and PX4 SIH's untimed log gets the live clock's.
- **On SITL.** On a 150 s `crossing` engagement, ArduCopter's clock drifts −0.16 ppm and its pairs are 0.13 ms RMS from the line. With 100 ppm injected either way and the middle 60 % withheld, every position aligns within 21 µs. Its positions arrive 0.78 s to 0.80 s after their aligned times: its simulated GNSS time is that far behind the host's clock. PX4 SIH's offsets spread over 1.9 s, so its sortie is not checked. On CI's runners, across the three engagements, ArduCopter aligns within 41 µs and PX4 SIH's pairs are 0.8 ms to 1.1 ms RMS from their line.
- **Tests.** Synthetic clocks cover every case, and [`test/fixtures`](timealign/test/fixtures/README.md) holds that engagement's clock pairs and position times, as the rig's own decoder read them, for the check. The fuzz target fits any pairs and checks that each fit and check keeps its promises.

## PLI store

[`services/plid/`](services/plid) is `ics-plid` (ICS-030, CPP-14), built on `ics::store` in [`store/`](store). It stores every vehicle's PLI from every feed, as the adapters make it, archives it as Parquet and answers queries for it.

| Header | Provides |
|---|---|
| [`segment_writer.hpp`](store/include/ics/store/segment_writer.hpp), [`segment_reader.hpp`](store/include/ics/store/segment_reader.hpp), [`segment_format.hpp`](store/include/ics/store/segment_format.hpp) | The segment log: `PliRecord` and `PliEvent` entries, each framed with its length and a CRC-32C, appended to a segment named for the UTC time it opened; a reader that stops at a torn tail, and `cut_torn_tail` |
| [`parquet_writer.hpp`](store/include/ics/store/parquet_writer.hpp), [`parquet_schema.hpp`](store/include/ics/store/parquet_schema.hpp), [`thrift.hpp`](store/include/ics/store/thrift.hpp) | ICS's own Parquet writer: one flat column per scalar field, nested fields named with dots (`position.latitude_deg`), enums as their names, PLAIN pages, no compression |
| [`archive.hpp`](store/include/ics/store/archive.hpp) | `archive_segment`: a closed segment to `<segment>.records.parquet` and `.events.parquet` beside it |
| [`query.hpp`](store/include/ics/store/query.hpp) | `answer`: a `QueryPliRequest` from the segments, earliest first, in batches |
| [`retime/`](retime) | `ics::retime`, which `ics-pli-import` and `ics-time-align` share: a sortie's captures sorted by vehicle and sortie, and its positions and onboard logs timed by the clock fits ([Time alignment](#time-alignment)) |

- **Feeds.** MAVLink and CoT come from the TAP ports, captured with host time stamps: datagrams to the CoT port go to the CoT adapter, and every other to the MAVLink adapter, as `ics-mavlink-replay` reads a capture. SAPIENT comes from a TCP connection to the middleware, which never blocks the service and reconnects after 1 s, doubling to 30 s. Lattice comes from its entity stream in a thread of its own, off until ICS-023's credentials arrive. Each feed's roles are `ID=ROLE` settings. `[capture] files` replays pcap files instead of TAP ports, for tests and the SITL rig.
- **The segment log.** Every step writes what the feeds gave to the current segment, where queries see it at once, and syncs it every `sync_interval_ns`, so a crash loses at most that much. A segment closes after `rotate_interval_ns`, or at the end of a sortie, when the last vehicle armed disarms, and the next opens. At start-up, each segment no run archived has its torn tail cut and is archived.
- **Parquet.** A closed segment is archived in a thread of its own: each file written as `.partial`, synced and renamed, so a file at its final name is whole. The writer's bytes are pinned by [`golden/pli`](../golden/pli), and pyarrow reads them back in [`test_pli_parquet.py`](../python/tests/test_pli_parquet.py).
- **Queries.** `PliQueryService` ([`pli_query.proto`](../proto/ics/v1/pli_query.proto)) over gRPC on a local Unix-domain socket (`query_socket`, #146): a client sends one `QueryPliRequest` and reads a stream of `QueryPliResponse` batches of at most 60 KiB, the last with `done` set. A request the store refuses ends the stream with `INVALID_ARGUMENT` and the reason. A query asks for records or events in a time range, optionally of one entity, up to 100,000. It reads only segments whose times overlap its range, from a catalog read again only when a segment grows.
  - Queries run side by side on up to 8 gRPC threads, each from its own copy of the catalog, so a client that stops reading holds up only its own query. Stopping `ics-plid` gives the queries running 1 s to finish, then cancels them.
  - The service code is generated at build time ([`proto/CMakeLists.txt`](proto/CMakeLists.txt)) by the protoc and `grpc_cpp_plugin` of the Conan packages the libraries come from, so it cannot drift from the gRPC library. Conan Center's gRPC recipe asks for protobuf below 7; gRPC 1.84.0 itself is built against protobuf 35.1, so the Conan profiles replace every protobuf in the graph with the 7.35.0 ICS pins.
- **Logs.** `started`; `sapient_connected` and `sapient_disconnected`; `lattice_refused`; `segment_closed`, `segment_recovered`, `segment_unusable`, `archived` and `archive_failed`; `replayed`, once every file is read; `query_answered` and `query_refused`; `store_failed`; `start_failed`, with the `reason`; and `stopped`, with the `signal`.
- **Running.** `ics-plid` takes no arguments. It reads `/etc/ics/ics-plid.toml`, a config like [`ics-plid.toml`](services/plid/ics-plid.toml). It runs until SIGINT or SIGTERM, closes and archives its segment, and exits 0. It exits 1 when it cannot start or store, and 2 for an argument or a bad config file.
- **Import.** `ics-pli-import` takes no arguments. Run it in the folder of one sortie's TAP captures (`*.pcap`) and onboard logs (`*.ulg`, `*.bin`) with `/etc/ics/ics-pli-import.toml`, a config like [`ics-pli-import.toml`](services/plid/ics-pli-import.toml). It stores, as one new segment, which it archives: each capture position timed by its sortie's clock fit (`PLI_TIME_BASIS_VEHICLE_ALIGNED`), beside the live records; and each log's records and events, timed by the sortie whose boot times they overlap.
- **Query tool.** `ics-pli-query records|events [START_UTC_NS END_UTC_NS [ENTITY]]` asks `/run/ics-plid/query` and writes each record or event as a JSON line, as `ics-mavlink-replay` does, then a `done` line. It exits 1 when the query failed or was truncated.
- **Done when.** Replaying the SITL `crossing` capture, `ics-plid` serves exactly the records and events the MAVLink adapter makes of it, ordered by time. Every night the [SITL rig workflow](../.github/workflows/sitl.yml) checks each engagement the same way with the real binaries ([`deploy/sitl`](../deploy/sitl/README.md#pli-store)).
- **Tests.** Every feed, rotation, recovery, archiving, queries and their batching, and the failures of each. The live CoT test captures on the loopback interface, so it needs `CAP_NET_RAW`. The segment reader's fuzz target reads any bytes as a segment, in memory and from a file, and checks that both agree.

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

## Camera service

[`services/camd/`](services/camd) is `ics-camd` (ICS-031, CPP-14). It drives every camera of a station, and serves `CameraService` ([`camera_service.proto`](../proto/ics/v1/camera_service.proto)) over gRPC on a Unix-domain socket. Its config is [`ics-camd.toml`](services/camd/ics-camd.toml), read from `/etc/ics/ics-camd.toml`.

- **One command.** [`ics-cam-offload RUN_ID [SOCKET]`](services/camd/include/ics/camd/command_client.hpp) sends `ArmAndOffload`. ics-camd then:
  1. makes the run's folder, `offload_folder/RUN_ID`, and refuses a run ID that is not a plain folder name or whose folder exists;
  2. watches ics-timingd's `TimeQualityService` (#150), waiting up to `time_wait_ns` for its first report;
  3. runs `record_and_offload` on every camera at once, one thread each, into `RUN_ID/CAMERA_ID`;
  4. writes each segment's `CameraFrameMeta` beside its cine, as `cine-NNN.meta`: length-delimited messages, one per frame.

  It answers with each camera's segments, frames, files and whether they verified. ics-cam-offload prints one JSON line per camera, and exits 0 only when every camera verified. One command runs at a time: another is refused with `FAILED_PRECONDITION`, since arming a camera again would discard the segments it has not yet saved.
- **Without ics-timingd.** When no report comes, the cameras still record, since an engagement cannot be repeated. Their frames are not IRIG-timed, so their segments do not verify, and an X6980 that stamps no year fails, as `FlirCamera` does without the station's UTC.
- **Camera SDKs.** [`drivers.hpp`](services/camd/include/ics/camd/drivers.hpp) is where a camera's SDK joins ics-camd. Each camera's `[camera.ID.driver]` table names its driver and holds that driver's settings; `emulated-phantom` and `emulated-x6980` exist today. A new SDK adds a settings struct, its name, its reader and its opener, all in `drivers.hpp` and `drivers.cpp`, and the build fails until each is there. The config, the offload and the gRPC service take any `SegmentCamera` and do not change. A driver that connects to its camera does so in `configure`, so a camera that cannot be reached fails only its own offload.
- **Done when.** One `ics-cam-offload` arms and offloads both emulated cameras through a running ics-camd, with ics-timingd's own report server: two verified segments of 300 frames each per camera, every cine and metadata file on disk (`CamdRun.OneCommandArmsAndOffloadsEveryCamera`).

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

## Mount client

[`mount/`](mount) is `ics::mount` (ICS-033, CPP-16): the client for PlaneWave Interface 4 (PWI4), which runs each station's mount, and the loop that sends it rate demands, with the sun keep-out interlock around both (ICS-034, CPP-17). `ics-mountd` (ICS-041) will run them. A run on a bench mount is [#165](https://github.com/MatthewK84/ICS/issues/165).

| Header | Provides |
|---|---|
| [`pwi4_status.hpp`](mount/include/ics/mount/pwi4_status.hpp) | `parse_status`: PWI4's `keyword=value` status, in ICS units; `MountStatus` and `AxisStatus` |
| [`limits.hpp`](mount/include/ics/mount/limits.hpp) | `check_goto` and `limit_rates`: a goto or rates held to the axis limits; `MountLimits` |
| [`pwi4_client.hpp`](mount/include/ics/mount/pwi4_client.hpp) | `Pwi4Client`: status, connect, enable, goto, rates and stop over PWI4's HTTP interface, with bounded retries |
| [`rate_loop.hpp`](mount/include/ics/mount/rate_loop.hpp) | `RateLoop`: the latest demand from a `RateSource`, sent at 20 to 50 Hz through the interlock, stopping the mount on a fault |
| [`sun.hpp`](mount/include/ics/mount/sun.hpp) | `sun_position`: the sun's azimuth and altitude at a site and UTC time, by NREL's Solar Position Algorithm |
| [`keep_out.hpp`](mount/include/ics/mount/keep_out.hpp) | `KeepOut` and the geometry of the keep-out: separations, the closest approach over a sweep, `clear_ahead`, `escape_rates`, `sun_safe_all_year` |
| [`interlock.hpp`](mount/include/ics/mount/interlock.hpp) | `SunGuard`, the keep-out's decisions, and `SunInterlock`, which every command to the mount goes through; `ClockSource` |

- **PWI4's interface.** The client uses only commands PlaneWave documents in its published client (`pwi4_client.py`): GET requests with their parameters in the query, each answered with status text. The commands are:
  - `/status`, `/mount/connect`, `/mount/enable`;
  - `/mount/goto_alt_az`, `/mount/stop`;
  - `/mount/offset`, with `axis0_set_rate_arcsec_per_sec`, `axis1_set_rate_arcsec_per_sec` and `axisN_stop_rate`.

  The stations' mounts are alt-az: axis0 is azimuth and axis1 altitude. Whether offset rates move a mount from rest is to be confirmed on a bench mount (#165).
- **This machine only.** PWI4 speaks only plain HTTP, so the client sends to nothing but `127.0.0.1`, on the configured port; there is no host setting. A PWI4 on another computer is reached through a local tunnel, such as SSH or TLS port forwarding, which also encrypts the hop across the station network. This is the Lattice client's rule too: plain HTTP only to this host.
- **Limits.** The client holds every command to them before sending it; the narrower of ICS's limits and the mount's mechanical ones applies.
  - A goto below or above the altitude limits is refused and never sent. A goto's azimuth wraps as the mount chooses, and the mount enforces its own azimuth limits.
  - Each rate is clamped to its axis's maximum. A rate that would carry an axis past a limit within 1 s is set to 0; a rate away from a limit is always allowed.
  - A demand that is not a number is taken as 0. Every change is reported.
- **Retries.** Every command the client sends is idempotent, so it is retried: by default 3 attempts, 5 ms then 10 ms apart. No attempt or wait passes the request's deadline. Set the timeout to 40% of the loop period, or less, so a slow answer leaves room for a retry.
- **The loop.** Each period asks for the status, then sends the latest demand, held to the limits against that status, all within the period. It stops the mount (zero rates, then `/mount/stop`) and returns when either of these happens, and never starts it moving again on its own:
  - no demand is fresher than 200 ms;
  - 3 periods in a row fail to reach the mount.

  When told to stop, it sends zero rates.
- **Done when.** A stable 20 to 50 Hz rate loop, here against an emulated PWI4 with two simulated axes. Over 10 s at 50 Hz and at 20 Hz, with a moving demand (`MountBench`):
  - a rate command each period, with at most 1% of periods late or missing theirs;
  - every command within the limits;
  - the axes following the demand.

  Runs here have had 0 to 3 late or missed periods in 500.
- **Tests and fuzzing.** Tests cover every refusal of the parser, each limit, retries, timeouts, dropped connections, and each fault. The fuzz target reads any status text.

### Sun keep-out interlock

The interlock (ICS-034) keeps the line of sight out of a 15° cone around the sun's centre. It keeps the mount out of a guard zone, the cone plus a 2° margin, which allows for pointing error, refraction and the sun's motion.

- **The sun's position.** `sun_position` is NREL's Solar Position Algorithm (Reda and Andreas, NREL/TP-560-34302, revised 2008), good to 0.0003°.
  - It takes UTC from 2000 to 2100, a configured ΔT (69 s), and a standard atmosphere for refraction.
  - Its tables of periodic terms ([`spa_terms.hpp`](mount/src/spa_terms.hpp)) were generated from pvlib python's copy of the report's tables, under pvlib's BSD-3-Clause licence ([`tools.txt`](mount/src/tools.txt) and the dependency register). NREL's report could not be reached from the development environment.
  - The tests check the report's worked example to 0.0001°, and eight cases against pvlib's SPA to 0.000001°. Locally, 100,000 random times and sites agreed with pvlib to within 0.000000001°.
  - The interlock works the sun out again each second.
- **Commands.** Every command to the mount goes through `SunInterlock`.
  - A goto whose target is inside the guard zone is refused, sending nothing.
  - Demanded rates are held to the axis limits, then zeroed if the motion over the lookahead (1 s) would enter the guard zone. The motion counts both the commanded rates and the mount's measured velocity.
  - A move that comes no nearer the sun is always allowed, so a mount inside the zone can always leave it.
- **Supervision, each period.**
  - **Stopping a motion headed in.** PWI4 does not say which way its slews go, so a goto is checked by its target only. The supervisor stops any motion, PWI4's slews included, whose path over the lookahead from the measured velocity would enter the guard zone: zero rates, then `/mount/stop`.
  - **Escaping.** When the mount is inside the guard zone (the sun moves up to 0.25° a minute), the interlock drives it straight away from the sun at 1°/s, within the limits, until it is 1° outside the zone. Then it stops and waits for a new command. This is the one time the interlock moves the mount on its own.
  - **Faults.**
    - A *clock fault*: ics-timingd reports the clock free-running, its error bound passes 1 s, or no new report comes for 30 s.
    - An *ephemeris fault*: a time outside 2000 to 2100, a sun that is not a number, or PWI4's own `mount.distance_to_sun_degs` differing from ours by more than 2° while PWI4 is connected.
    - A *blocked escape*: the limits leave no way away.

    On any of these, the interlock sends zero rates and `/mount/stop`. Then it sends `/mount/park`, if the way from here to the park position stays outside the guard zone around the last good sun, grown by how far the sun can have moved since. Otherwise the mount holds where it stopped. The park slew is watched in turn and stopped if it heads in. A fault latches until an operator clears it.
  - **The park position**, PWI4's, is given in the configuration. It must be outside the guard zone all year: its declination at least 23.5° plus the guard zone from the equator. The interlock refuses to start otherwise. The pole-ward horizon 10° up passes below about 50° latitude.
- **The clock.** `ClockSource` gives the interlock UTC and the clock's health from ics-timingd; `ics-mountd` will feed it, so `ics::mount` does not depend on gRPC. Until the first good report the interlock refuses every command.
- **The rate loop** runs through the interlock. A period in which the interlock acted sends no demand. On the interlock's fault the loop returns, leaving the mount to the interlock.
- **Done when.** Randomized commands never enter the cone, and a fault parks the mount. Here it is tested against a simulated mount and the emulated PWI4; the bench mount is [#165](https://github.com/MatthewK84/ICS/issues/165).
  - **10,000 trials in simulated time** (`SunTrials`). Gotos and rate demands at random sites and times run at 20 to 50 Hz, many aimed through the sun or at it. The simulated mount has accelerating axes, and its gotos go either way round in azimuth. The closest approach was 15.50°, in a trial that started just inside the guard zone and escaped.
  - **500 fault trials in simulated time.** Each injects a clock or ephemeris fault. Two thirds parked; the rest held, all outside the cone.
  - **20 trials end to end** (`SunBench`), through the client to the emulated PWI4 in real time. The closest approach was 20.6°. A clock fault and an ephemeris fault each park the emulated mount.

## Encoder time-tagger

[`tagger/`](tagger) is `ics::tagger` (ICS-032, FW-01): the encoder time-tagger's core, the part that runs the same on any board. It is freestanding C++20, allocating nothing after start-up and throwing nothing, so it builds for the tagger's MCU as well as here. The board port is [`firmware/encoder_tagger`](../firmware/encoder_tagger) ([#163](https://github.com/MatthewK84/ICS/issues/163)).

| Header | Provides |
|---|---|
| [`counter.hpp`](tagger/include/ics/tagger/counter.hpp) | `CounterExtender`: the MCU's 32-bit counter as 64 bits, through wraps and readings slightly out of order |
| [`pps_clock.hpp`](tagger/include/ics/tagger/pps_clock.hpp) | `PpsClock`: UTC on the counter from the PPS edge and the host's label; each reading's time, bound and state (acquiring, locked or holdover) |
| [`sampler.hpp`](tagger/include/ics/tagger/sampler.hpp) | `Sampler`: both encoders' counts, stamped, in batches of 10 |
| [`datagram.hpp`](tagger/include/ics/tagger/datagram.hpp) | `encode` and `decode`: a batch as a 256-byte UDP datagram with a sequence number and a CRC-32C; `angle_rad` |

- **The clock.** How it accepts edges, locks and holds over is in [Encoder clocks](../docs/frames-and-time.md#encoder-clocks).
- **Done when.** C18, time-tag error ≤50 µs against injected PPS, on an emulated bench (`TaggerBench.MeetsC18AgainstInjectedPps`). It runs 10 minutes at 1,000 samples a second through the datagram, with:
  - a crystal 20 ppm fast and wandering;
  - 30 ns of PPS jitter;
  - a glitch edge every 37 s;
  - 20 s without PPS.

  The worst locked error is 244 ns, and every sample is within its bound. A second run uses a crystal 45 ppm slow and 200 ns of jitter.
- **Tests and fuzzing.** Tests cover every refusal of the decoder and that nothing allocates. The fuzz target decodes any bytes, and re-encodes what it reads to the same bytes.

## Encoder module

[`encoder/`](encoder) is `ics::encoder` (ICS-036, CPP-15): each camera frame's line of sight. It reads the time-tagger's datagrams and keeps the last seconds of axis angles. It interpolates them to a frame's mid-exposure, and applies the station's pointing model, fitted by [`ics_models.pointing`](../python/README.md#pointing-model) (ICS-035). Its users are `ics-mountd` (ICS-041), which receives the tagger's datagrams, and registration (CPP-23), which asks for frames. A run on the real tagger and bench mount is [#165](https://github.com/MatthewK84/ICS/issues/165).

| Header | Provides |
|---|---|
| [`pointing_model.hpp`](encoder/include/ics/encoder/pointing_model.hpp) | `PointingModel`, `to_sky` and `to_encoder`: ICS-035's 16 terms, sign for sign; `load_pointing_model`: the parameter file, with its SHA-256 for the run record |
| [`angles.hpp`](encoder/include/ics/encoder/angles.hpp) | `EncoderConfig` and `angles_from`: a sample's counts as axis angles |
| [`track.hpp`](encoder/include/ics/encoder/track.hpp) | `AngleTrack`: the newest samples, counting lost batches, and `at`: the angles, rates and bound at any instant they span |
| [`line_of_sight.hpp`](encoder/include/ics/encoder/line_of_sight.hpp) | `line_of_sight`: a frame's `CameraFrameMeta` to its line of sight at mid-exposure |
| [`receiver.hpp`](encoder/include/ics/encoder/receiver.hpp) | `DatagramReceiver`: the tagger's UDP datagrams, decoded, counting what is malformed |
| [`config.hpp`](encoder/include/ics/encoder/config.hpp), [`frame_meta.hpp`](encoder/include/ics/encoder/frame_meta.hpp), [`monitor.hpp`](encoder/include/ics/encoder/monitor.hpp) | The `[encoder]` table; `.meta` files; `ics-encoder-monitor`'s reports and command |

- **The pointing model.** `parse_pointing_model` reads the file with protobuf's JSON parser into a `google.protobuf.Struct`, as the Lattice decoder does. It checks:
  - format `ics-pointing-model`, version 1;
  - a `model_id` and `station_id`;
  - one to 16 terms, each named once, with a finite value and a sigma of 0 or more.

  `load_pointing_model` also records the file's SHA-256 through OpenSSL, for the run record's `parameter_files`. Sky to encoder is ICS-035's fixed-point iteration: ten passes at most, to 1e-13 rad. Both directions match `golden/pointing` to 1e-12 rad.
- **Angles.** Each axis's counts are linear in angle, from the encoder's own zero: `AxisConfig` sets the counts a revolution and the counting direction. The model's IA and IE carry the zero's offset from north and the horizon, so the zero must be the same at every power-up. The tagger's board reads incremental A/B/Z encoders, so the board port must zero each counter at its index pulse ([#163](https://github.com/MatthewK84/ICS/issues/163)). Azimuth wraps to [0, 2π), so a single-turn encoder reads the same as one that counts on.
- **The history.** `AngleTrack` keeps 10 s at 1,000 samples a second, in a ring allocated once. It drops samples whose clock is still acquiring, and samples no later than the newest. It counts lost batches from the sequence numbers, and sequence resets, as from a tagger that started again.
- **Interpolation.** Linear between the samples either side, which at 1 kHz is within 0.13 µrad for accelerations up to 1 rad/s². It is refused outside the history, and when the samples either side are more than 10 ms apart, as across a lost batch. Holdover samples are used.
- **The bound.** Each line of sight carries one, on the sky: the larger time bound of the two samples times the rate on the sky, plus one count of each axis. The pointing model's own error (C17) and the frame's time error are not in it.
- **Frames.** Only frames timed by IRIG-B are taken, at mid-exposure (start plus half the duration). A frame the host stamped on arrival is refused: at 0.1 rad/s a millisecond is 100 µrad.
- **Production connections.** Each input the bench stands in for has a named place for the real one:

  | Input | In operation | On the bench |
  |---|---|---|
  | The tagger's datagrams | The board ([#163](https://github.com/MatthewK84/ICS/issues/163)) sends to `tagger_address`:`tagger_port` in the `[encoder]` table | ICS-032's core, against the emulated station, sending to `127.0.0.1` |
  | The encoders | `azimuth_counts_per_rev`, `elevation_counts_per_rev` and the `_reversed` settings, from the mount's encoders | 2²⁴ counts a revolution; azimuth single-turn, elevation reversed |
  | The pointing model | `pointing_model_file`, written by `python -m ics_models.pointing fit` from a star night (ICS-093); its `station_id` must match | `golden/pointing/model.json` |
  | Frame times | `CameraFrameMeta` from `ics-camd`, timed by IRIG-B (ICS-031), in the `.meta` file beside each segment | Frames at random times and exposures, within 1 µs |

  The sample config is [`ics-encoder.toml`](encoder/ics-encoder.toml); `ics-mountd` will read the same `[encoder]` table.
- **Tool.** [`ics-encoder-monitor`](encoder/tool/main.cpp) `SECONDS` is the operational test of this path against the real tagger.
  - It reads `/etc/ics/ics-encoder.toml` and loads the pointing model, refusing one for another station.
  - It listens for 1 to 600 s. Once a second it logs `encoder_status`: datagrams, malformed and lost batches, resets, the newest sample's clock state and bound, its axis angles and line of sight, and how far it lags the host's clock.
  - Then it logs the line of sight of each frame in the `.meta` files in the working folder, or why it was refused. Run it in the folder `ics-camd` offloads to, for longer than the recording and the offload take.
  - It exits 0 when it heard the tagger, 1 when the model, the socket or a `.meta` file could not be used, 2 for a bad argument or config file, and 3 when no batch came.
- **Done when.** 5 µrad or less at 0.1 rad/s on the bench. The bench is emulated (`EncoderBench`), and runs for a minute:
  - ICS-032's tagger core, against the emulated station's crystal and PPS, with 20 s without PPS;
  - its datagrams over UDP, one in 97 lost;
  - a simulated mount slewing both axes at 0.1 rad/s and reversing at up to 1 rad/s², the azimuth through north;
  - 2²⁴-count encoders;
  - a frame each batch, at a random time in the last 1.5 s and with a random exposure.

  Each line of sight is compared with the truth: the model applied to the true axis angles at the true mid-exposure. Over 5,543 frames the worst error was 0.58 µrad, in holdover, and every frame was within its bound. The only frames refused were those in a lost batch's gap and those the host timed. A second run uses a slow, noisy crystal.
- **Tests and fuzzing.** Tests cover every refusal of the loader and the frame query, the 10 ms rule, wraps, lost and repeated batches, the receiver over loopback, the config, and the monitor's reports and exit codes. The fuzz targets read any text as a parameter file and any bytes as a `.meta` file.

## Cue extrapolator

[`cue/`](cue) is `ics::cue` (ICS-037, CPP-18): where a station points to find the target. It fuses the target's position reports, PLI from its own GNSS (over MAVLink, CoT or Lattice) and radar tracks (over SAPIENT), in a constant-acceleration Kalman filter in the range ENU frame. It gives the station's azimuth, elevation, their rates and a one-sigma pointing uncertainty, predicted to the moment a mount command sent now takes effect. Its users are the rate loop (ICS-033), then the tracker and `ics-mountd` (ICS-038, ICS-041). It is the first module held to the [JSF AV rules](#jsf-av-checks), and [`ics_models.cue`](../python/README.md#cue-extrapolator) is its reference: the C++ matches [`golden/cue`](../golden/README.md) in every outcome, within 1e-9 in every state and covariance value and 1e-10 rad in every angle.

| Header | Provides |
|---|---|
| [`kinematics.h`](cue/include/ics/cue/kinematics.h) | `Estimate`, the nine-number state and its covariance; `predict`, with white-jerk process noise; the fixed-size matrix arithmetic and `inverse_symmetric` |
| [`measurement.h`](cue/include/ics/cue/measurement.h) | `measurement_from`: an `ics.v1.PliRecord` as a position, with sigmas and a velocity when it has one, in the range frame; `Noise_defaults` for a record that carries no sigmas |
| [`filter.h`](cue/include/ics/cue/filter.h) | `Cue_filter`: `add`, which gates and re-filters, and `estimate_at`, which refuses past the coast limit |
| [`pointing.h`](cue/include/ics/cue/pointing.h) | `Station`, `Cue` and `cue_for`: an estimate as a station's cue, in its own ENU axes |
| [`extrapolator.h`](cue/include/ics/cue/extrapolator.h) | `Cue_extrapolator`: `take` each record the station hears, and `cue_at` a command's send time |
| [`config.h`](cue/include/ics/cue/config.h), [`replay.h`](cue/include/ics/cue/replay.h) | The `[cue]` table; `ics-cue-replay`'s work |

- **The filter.** Each axis is a constant-acceleration model, its acceleration driven by white jerk: one density for east and north, another for up. The sUAS the range flies manoeuvre at up to 1 g, and the cue looks at most about 300 ms ahead, so no IMM or Singer model is needed. A report updates the position, and the velocity when it has one, in Joseph form. The first report starts the estimate at its position and velocity, with no acceleration.
- **Late reports.** Reports arrive on UTC, aligned by ICS-026, but out of order across sources: radar lags GNSS. The filter keeps the last 2 s of reports in time order, each with its checkpoint, the estimate after it. A late report goes in its place, and every report from there on is applied again from the checkpoint before it, so the estimate is the one in-order reports would give. A report older than the history, or than the checkpoint of the last report dropped, is refused as too old. Everything is in fixed arrays of 64 reports, so nothing is allocated.
- **The gate.** A report whose position is more than 16.27 from the prediction, by the chi-squared distance with three degrees of freedom (99.9%), is held but not applied, and counted. There is no data association: the operator marks which reports are the target through each adapter's role assignment (`ENTITY_ROLE_TARGET`), and the cue takes only those.
- **Pointing.** The cue is in the station's own ENU axes, which turn from the range frame's by about 50 µrad at 300 m. Azimuth is clockwise from north in [0, 2π). The pointing uncertainty is the larger axis of the position covariance across the line of sight, over the range. A target within 1 cm of the station's vertical, or within 1 m of the station, has no direction to cue.
- **Latency.** `cue_at(now)` predicts to now plus the mount's command latency, from the newest report's checkpoint, so the age of the newest report is covered too. It refuses a prediction more than the coast limit, 2 s, past the last report.
- **Production connections.** Each input the tests stand in for has a named place for the real one:

  | Input | In operation | In the tests |
  |---|---|---|
  | The target's PLI | ics-plid's segment files (`.icspli`, ICS-030), from the MAVLink, CoT and Lattice adapters, with the target's role set in each adapter's config | Synthetic `PliRecord`s at 10 Hz, as the MAVLink adapter gives them |
  | Radar tracks | The SAPIENT adapter (ICS-024), into the same segment files | Synthetic: 2 Hz, 3 m, 300 ms late, a fifth lost |
  | The station and the range | `station_*` and `range_origin_*` in the `[cue]` table: the mount's surveyed position, and the range ENU origin every module takes | Three stations 250 to 300 m from the crossing point |
  | The command latency | `command_latency_ns`, measured on the mount (ICS-033) | 50 ms |
  | Sigmas | A record's own, such as GPS_RAW_INT's accuracies; else the `[cue]` defaults by fix type | 0.5 m and 0.8 m for PLI, 3 m for radar |

  The sample config is [`ics-cue.toml`](cue/ics-cue.toml); `ics-mountd` will read the same `[cue]` table.
- **Tool.** [`ics-cue-replay`](cue/tool/main.cpp) is the operational test of the cue. It reads `/etc/ics/ics-cue.toml`, then replays the segment files in the working folder in the order their records were received, live or after the run. Every 20 ms it writes the station's cue as a JSON line, with the counts of reports taken, late, gated, too old, not the target and unusable. It exits 0 when it replayed, 1 when the config, the sites or the segments could not be used, and 2 for an argument.
- **Done when.** Pointing error under 2 mrad on replayed tracks ([`replay_test.cpp`](cue/test/replay_test.cpp)). No SITL record of the crossing is in the repository, so the target flies [`crossing.toml`](../deploy/sitl/engagements/crossing.toml)'s path synthetically, with a full-rate truth table as PX4's onboard log would be. From 3 s on, each 50 Hz cue is compared with the truth at the moment its command takes effect, from three stations:

  | Track | Cues per station | Largest error | RMS |
  |---|---|---|---|
  | The crossing: climb to 35 m, then east at 5 m/s, 90 s | 4,348 | 0.10 to 0.17 mrad | 0.03 to 0.04 mrad |
  | Manoeuvring: 15 m/s, turns of 1 g, 30 s | 1,348 | 0.49 to 1.53 mrad | 0.12 to 0.22 mrad |

  Replaying a flown SITL crossing is [#172](https://github.com/MatthewK84/ICS/issues/172), and the field replay with a real radar is in [#93](https://github.com/MatthewK84/ICS/issues/93).
- **Tests.** Besides the golden vectors and the replay: exact constant-acceleration truths; late reports giving the in-order estimate; the gate, the history, the capacity and the coast limit; every refusal of a record; the station axes, the angles, their rates against differences and the sigma; the config; and the tool on segment files from ics-plid's own writer. No new parser reads outside data: the segment reader (ICS-030) and the config reader are fuzzed already.

## Tracker

[`tracker/`](tracker) is `ics::tracker` (ICS-038, CPP-19), the closed-loop tracker for one station. It streams the narrow-field tracking camera through [Aravis](https://github.com/AravisProject/aravis) (GigE Vision), finds the target's sub-pixel centroid in a window around where it should be, tracks its sky direction and rate, and gives the rate loop (ICS-033) a demand: the tracker is a `mount::RateSource`. It follows the cue (ICS-037) until it sees the target, takes over once the target is held, coasts through short dropouts, and falls back to the cue to reacquire. It keeps the [JSF AV rules](#jsf-av-checks), and [`ics_models.tracker`](../python/README.md#tracker) is its reference: the C++ matches [`golden/tracker`](../golden/README.md) in every outcome and window, and within 1e-9 in every number.

| Header | Provides |
|---|---|
| [`camera.h`](tracker/include/ics/tracker/camera.h) | `Tracking_camera`: `start`, `next`, `release`, `stop`; `Camera_settings`; `Frame`, a view of a buffer allocated before streaming starts |
| [`aravis_camera.h`](tracker/include/ics/tracker/aravis_camera.h) | `Aravis_camera`, the binding; `frame_of`, a buffer as a frame |
| [`centroid.h`](tracker/include/ics/tracker/centroid.h) | `window_around`; `Centroider::find`, which allocates nothing per frame |
| [`geometry.h`](tracker/include/ics/tracker/geometry.h) | `Camera_geometry`; pixels to tangent-plane offsets and sky directions, and back |
| [`estimator.h`](tracker/include/ics/tracker/estimator.h), [`demand.h`](tracker/include/ics/tracker/demand.h) | `Angle_estimator`, an alpha-beta track of azimuth and elevation; `demand` |
| [`tracker.h`](tracker/include/ics/tracker/tracker.h) | `Tracker`: `take` each frame on the camera's thread, `latest` on the rate loop's; the modes; `Axis_source`, `Cue_source` and `Time_source` |
| [`config.h`](tracker/include/ics/tracker/config.h), [`monitor.h`](tracker/include/ics/tracker/monitor.h) | The `[tracker]` table; `ics-tracker-monitor`'s work |

- **The camera.** `Aravis_camera` applies the `features` list first (for example `PtpEnable=true`), then the pixel format (Mono8 or Mono16), region, frame rate and exposure, and streams into buffers ICS allocates before it starts. A frame's time is the camera's time stamp, PTP-locked to ics-timingd, or for a camera without PTP the host's receive time; either is corrected by `stamp_offset_ns`. A buffer the camera could not fill is returned to the stream and counted as dropped. Aravis and GLib are LGPL-2.1-or-later, linked dynamically and shipped unmodified ([register](../docs/dependency-register.toml)); their C API is JSF deviation D-8, confined to `aravis_camera.cpp`.
- **The centroid.** The background and its noise are the median of the window's border and its MAD × 1.4826. Pixels that differ from it by more than the larger of `k_sigma`·σ and `min_contrast`, darker, brighter or either by `polarity`, form 8-connected blobs, and the contrast-weighted centroid of the blob nearest the prediction is the target, if its area is between `min_area` and `max_area`.
- **The geometry.** The camera rides the elevation axis, so its image does not turn against the mount's axes. A calibrated boresight pixel, plate scale, roll and mirroring turn a pixel into a tangent-plane offset, and the gnomonic projection about the mount's line of sight, through its pointing model (ICS-036), turns that into a sky direction.
- **The loop.** Each detection updates an alpha-beta track of azimuth and elevation (stable for 0 < α ≤ 1 and 0 ≤ β < 4 − 2α). The demand is for the moment a command sent now takes effect: the target's rate plus `gain` times the pointing error, with the mount carried on at its own rates to the same time, in the mount's axes. `latest` is shared under a mutex.
- **The modes.** Cueing searches the acquisition window around the cue, or the boresight with no cue; a detection starts acquiring. Acquiring searches the track window, and `confirm_frames` detections within `gate_px` make it tracking; a miss returns to cueing. Tracking coasts on a miss, and coasting returns to cueing after `coast_limit_ns` without a detection. Cueing demands follow the cue, and with no cue there is none, so the rate loop's staleness check stops the mount.
- **Production connections.** Each input the tests stand in for has a named place for the real one:

  | Input | In operation | In the tests |
  |---|---|---|
  | The camera | `camera` in the `[tracker]` table: the GigE Vision camera's Aravis ID, with `fake_camera = false` and `PtpEnable=true` in `features` | Aravis's fake GigE Vision camera, `Fake_1`; synthetic Mono16 frames |
  | The mount's axes | `Axis_source`: ics-mountd (ICS-041) answers from the encoder track its receiver fills (ICS-036) | A still mount |
  | The pointing model | The encoder module's model, from the fitted parameters (ICS-035) | The model with no terms |
  | The cue | `Cue_source`: ics-mountd answers from its `Cue_extrapolator` (ICS-037) | A scripted cue, or none |
  | UTC now | `System_time`, the host's clock, which ics-timingd keeps on UTC | A manual clock |
  | The camera geometry | `boresight_x`, `boresight_y`, `plate_scale_urad`, `roll_deg` and `mirrored`, calibrated on the station | The sample's values |
  | The command latency | `command_latency_ns`, measured on the mount (ICS-033) | 50 ms |

  The sample config is [`ics-tracker.toml`](tracker/ics-tracker.toml); `ics-mountd` will read the same `[tracker]` table.
- **Tool.** [`ics-tracker-monitor FRAMES`](tracker/tool/main.cpp) is the operational test of the camera and centroid path before ics-mountd closes the loop. It reads `/etc/ics/ics-tracker.toml` and streams FRAMES frames, searching around the boresight, then around the last detection. Each frame is a JSON line: its number, time, whether the target was found, where in pixels and microradians from the boresight, and its area, peak, background and σ. A frame the camera could not fill is an error line, and the last line is the count dropped. It exits 0 when it streamed, 1 when the config or the camera could not be used, and 2 for an argument.
- **Done when.** Every part has unit tests, the C++ matches the golden vectors, and the binding and the tool run against Aravis's fake camera in CI. C19, the target held in the central half of the narrow field at 500 m, is measured before release, in the closed-loop simulation and [#172](https://github.com/MatthewK84/ICS/issues/172)'s flown-crossing replay ([#175](https://github.com/MatthewK84/ICS/issues/175)), and flown in [#94](https://github.com/MatthewK84/ICS/issues/94).
- **Tests.** Besides the golden vectors: the fake camera's frames, settings, refusals, dropped buffers and stop; centroids of known spots at every polarity, the area limits and the nearest blob, with no allocation per frame; every mode change, the gate, coasting and its limit, frames without the mount's axes or out of order, and cues off the frame or behind; the config and its refusals; and the tool. No new parser reads outside data: frames come from the camera as fixed-size pixels, and the config reader is fuzzed already.

## Power-of-Ten checks

The C++ rules from the coding-standards table in [docs/build-plan.md](../docs/build-plan.md) are enforced by tools, not review (ICS-005):

| Rule | Enforced by |
|---|---|
| No goto; no recursion; functions of 50 lines or fewer; no owning raw pointers or malloc; `std::span` over pointer arithmetic; no mutable globals | clang-tidy, configured in [`.clang-tidy`](.clang-tidy); every finding is an error |
| `[[nodiscard]]` results are used; no implicit narrowing | `-Wall -Wextra -Wpedantic -Wconversion -Werror` for GCC and Clang, in [`cmake/Warnings.cmake`](cmake/Warnings.cmake) |
| No suppressions | [`policy/check-policy.sh`](policy/check-policy.sh) rejects `NOLINT`, `diagnostic ignored` pragmas, `-Wno-` flags and any extra `.clang-tidy` file |
| No allocation after initialization | [`ics::testing::NoAllocationScope`](testing/alloc_guard/include/ics/testing/no_allocation_scope.hpp) fails a test that allocates inside it |

Run it inside the `ics-cpp` image:

```sh
cpp/policy/check-policy.sh          # everything
cpp/policy/check-policy.sh gcc      # the GCC build and its seeds
cpp/policy/check-policy.sh clang    # the Clang build, clang-tidy and their seeds
```

CI runs the `clang` half (#159): every build holds both compilers to `-Werror`, and the GCC reproducible build compiles all the ICS code. Both halves check for suppressions. The script also proves each rule still fires: every file in [`policy/seeded/`](policy/seeded) breaks exactly one rule and must be rejected with the diagnostic named on its first line. The seeds build only with `-DICS_POLICY_SEEDS=ON`.

Add tests with `ics_add_gtest(name SOURCES … LIBRARIES …)` from [`cmake/Testing.cmake`](cmake/Testing.cmake). It links GoogleTest and the allocation guard, which replaces the global `operator new` and `delete` so that allocations are counted. Open a `NoAllocationScope` once a component is initialized and run its steady-state path inside it.

Two exceptions are deliberate:

- The allocation hook, [`testing/alloc_guard/src/allocation_hook.cpp`](testing/alloc_guard/src/allocation_hook.cpp), is the only file allowed to call `malloc`; its own `.clang-tidy` turns off `cppcoreguidelines-no-malloc` there and nowhere else. It marks ownership with `gsl::owner`, so `cppcoreguidelines-owning-memory` still applies.
- The `clang-tsan` preset links Clang's shared TSan runtime (`-shared-libsan`), because the static runtime defines `operator new` and `delete` itself and would clash with the allocation hook. GCC's TSan runtime is already shared.

Not enforced here: "no exceptions on real-time paths" needs `-fno-exceptions` on the real-time targets once they exist; the common headers already build that way. Coverage is gated only for the code listed in [`policy/coverage-gates.txt`](policy/coverage-gates.txt) (see [Test stages](#test-stages)), so other code is not yet measured against the Definition of Done's 90%.

## JSF AV checks

From ICS-037 ([#37](https://github.com/MatthewK84/ICS/issues/37)) on, every new module also follows the [JSF AV C++ Coding Standards](https://www.stroustrup.com/JSF-AV-rules.pdf). [docs/cpp-jsf-av.md](../docs/cpp-jsf-av.md) says how each of its 229 rules is checked, and keeps the deviations register and the review checklist. The modules written before ICS-037 come to these rules under [#170](https://github.com/MatthewK84/ICS/issues/170).

| What | Where |
|---|---|
| The JSF files | [`policy/jsf-paths.txt`](policy/jsf-paths.txt), one folder or file a line |
| Text checks and clang-query matchers, and the clang-tidy run | [`.github/scripts/cpp_jsf.py`](../.github/scripts/cpp_jsf.py) |
| The clang-tidy profile | [`policy/jsf-clang-tidy.yaml`](policy/jsf-clang-tidy.yaml), passed with `--config`, so it adds no `.clang-tidy` file |
| The extra compiler warnings | [`cmake/Jsf.cmake`](cmake/Jsf.cmake): `ics_jsf_target(<target>)`, or `ics_jsf_sources(<files>)` for JSF files in an older target |
| The fixed-size types | [`ics/common/universal_types.h`](common/include/ics/common/universal_types.h): `int8` to `int64`, `uint8` to `uint64` for bit patterns, `float32` and `float64` |
| The seeds | [`policy/seeded/jsf/`](policy/seeded/jsf): 38 the gate must reject and 2 the compiler must reject, each naming its finding on its first line |

`check-policy.sh clang` runs the gate over the JSF files and then over each seed. Both halves build the compiler seeds. To run the gate alone after building the `clang-debug` preset:

```sh
python3 .github/scripts/cpp_jsf.py check --root . --build cpp/build/clang-debug --paths cpp/policy/jsf-paths.txt
```

## Test stages

The dynamic stages run through [`policy/check-dynamic.sh`](policy/check-dynamic.sh) (ICS-008). CI runs the Clang sanitizers and coverage on every change to `cpp/`; the GCC sanitizers and fuzzing run by hand (#159):

| Stage | How | Seeded defect it must catch |
|---|---|---|
| AddressSanitizer | `gcc-asan` and `clang-asan` presets; every test must pass | a heap buffer overflow |
| UndefinedBehaviorSanitizer | `gcc-ubsan` and `clang-ubsan` presets, stopping at the first undefined behaviour | a signed integer overflow |
| ThreadSanitizer | `gcc-tsan` and `clang-tsan` presets | a data race |
| libFuzzer | `clang-fuzz` preset (libFuzzer with ASan and UBSan), by hand, for as long as given per fuzz target | a heap overflow behind a four-byte magic prefix, which must be found within two minutes |
| Coverage (ICS-015) | `clang-coverage` preset (Clang source-based coverage): every line and branch of each path in [`policy/coverage-gates.txt`](policy/coverage-gates.txt) covered, and its assertion density at or above its floor | a branch no test takes, and a function with no `ics::check` under a floor of 1 |

```sh
cpp/policy/check-dynamic.sh sanitizers             # all six presets
cpp/policy/check-dynamic.sh sanitizers gcc-asan    # one preset
cpp/policy/check-dynamic.sh fuzz 60                # seconds per fuzz target
cpp/policy/check-dynamic.sh coverage
```

The fuzz stage builds only the fuzz targets and runs as many at once as there are processors, so each still gets its full time. In CI, the [C++ toolchain workflow](../.github/workflows/cpp-toolchain.yml) runs six stages as their own jobs, side by side: the GCC reproducible build, the three Clang sanitizers, coverage and the Clang policy half.

Each job runs in the versioned build image of its dependency configuration (#159): the `ics-cpp` toolchain with that configuration's Conan packages, named by a hash of the files that decide its contents ([`deploy/toolchain/image-key.sh`](../deploy/toolchain/image-key.sh)). The [image workflow](../.github/workflows/conan-deps.yml) builds an image only when GHCR has none for its key, signs it with its SBOM and provenance, and verifies the signature then, once. The jobs pull their image through [`.github/actions/build-image`](../.github/actions/build-image/action.yml) and download nothing else. A pull request's own images serve only that pull request, and main uses only images built on main.

The assertion density of a gated path is the number of `ics::check` calls in its functions longer than three lines, divided by the number of those functions. Power of Ten rule 5 asks for two per function; container code, with one failure mode per operation, sits near 0.5, and logging and config, where bad input is an expected outcome rather than a bug, sit lower. Frames code sits at 0: its inputs are checked once, where they are made, and any further check could never fail, so its failure branch could never be covered. So each path has a floor that only rises: CI fails below it, and asks for the floor to be raised when the density rises above it. [`.github/scripts/cpp_coverage.py`](../.github/scripts/cpp_coverage.py) reads function extents from `llvm-cov export`, so a template member counts only once some test instantiates it. Add a library to the gates file when it is written, at its measured density. Test and fuzz folders are never gated. Comparisons in gated code are written out rather than defaulted, because Clang 17's coverage miscounts the branches of a defaulted comparison. The `clang-coverage` preset counts atomically (`-fprofile-update=atomic`): ics-plid's tests run the store's code on several threads at once, and plain counters lose increments there, which made a branch `store_test` takes read as never taken, and could make a branch no test takes read as taken.

The seeded defects live in [`policy/seeded-runtime/`](policy/seeded-runtime), each naming on its first line the report it must produce.

The `asan`, `tsan` and `fuzz` presets link dependencies built with the same sanitizer, from the [`asan`](conan/profiles/asan) and [`tsan`](conan/profiles/tsan) Conan profiles: protobuf and abseil annotate their containers for ASan only when they are built with it, and TSan cannot see synchronization in uninstrumented code. Mixing instrumented and uninstrumented code gives false reports. The `ubsan` presets use the plain debug dependencies, since UBSan checks only the code it instruments. A build folder configured before this change keeps its old toolchain file; configure it again with `cmake --preset <name> --fresh`.

Add a fuzz target with `ics_add_fuzzer(name SOURCES … LIBRARIES … CORPUS folder)` from [`cmake/Fuzzing.cmake`](cmake/Fuzzing.cmake), with a few small inputs in the corpus folder; see [`toolchain_check/fuzz`](toolchain_check/fuzz). Every build compiles fuzz sources, so the warnings and clang-tidy cover them; the `clang-fuzz` preset links them with libFuzzer. A crash fails the fuzz stage and keeps the crashing input in `ARTIFACT_DIR`, if set; reproduce it with the fuzz target and the input file as its only argument.

CodeQL analyzes the C++ code too, with the Python and TypeScript code; see [`.github/workflows/codeql.yml`](../.github/workflows/codeql.yml).

The SITL rig that the MAVLink adapter ([ICS-021](https://github.com/MatthewK84/ICS/issues/21)) is tested against is in [`deploy/sitl`](../deploy/sitl/README.md) ([ICS-018](https://github.com/MatthewK84/ICS/issues/18)).

Next issue: [ICS-019](https://github.com/MatthewK84/ICS/issues/19) (ics-timingd).
