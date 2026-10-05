# deploy/sitl

The SITL rig ([ICS-018](https://github.com/MatthewK84/ICS/issues/18), build-plan item INT-01): PX4 and ArduPilot software-in-the-loop simulators in containers, flying scripted target and interceptor profiles, with their MAVLink mirrored to an emulated network TAP port. The nightly [SITL rig workflow](../../.github/workflows/sitl.yml) flies three scripted engagements and fails if any of them does.

The rig is test equipment. Nothing here ships, and the autopilot images are never published: ArduPilot is GPL-3.0, and ICS does not distribute it.

## What an engagement is

An engagement file in [`engagements/`](engagements) scripts two quadrotors: a target and an interceptor, one flown by PX4 and the other by ArduPilot. The three files cover a tail chase, a head-on pass and a crossing, and between them each autopilot flies both roles. Every point is around a made-up range origin at 40° N, 100° W, the `range-origin` of [`golden/frames`](../../golden/frames).

The rig, [`python -m ics_sitl`](../../python/ics_sitl), is the ground station for both vehicles. For each vehicle it:

1. waits for the autopilot's heartbeat, and checks it names the expected autopilot;
2. asks for the messages it records, at fixed rates: `GLOBAL_POSITION_INT`, `GPS_RAW_INT`, `ATTITUDE_QUATERNION`, `SYSTEM_TIME` and `MISSION_CURRENT`;
3. uploads the mission with MAVLink's mission protocol: take off, set the cruise speed, wait at the start point (an unlimited loiter), then the path;
4. arms and starts the mission, and waits until the vehicle hovers at its start point.

When both vehicles wait at their start points, it moves both missions to their first path waypoint in the same instant (`MISSION_SET_CURRENT`), so the engagement starts together. Each vehicle is done when its autopilot reports reaching the last waypoint.

An engagement **passes** when, within its time limit:

- both autopilots report reaching every path waypoint, and each track passes within the file's `waypoint_tolerance_m` of each one;
- the closest approach between the vehicles falls within the file's `miss_distance_m`;
- each autopilot sent every message the MAVLink adapter ([ICS-021](https://github.com/MatthewK84/ICS/issues/21)) reads: `HEARTBEAT`, `GLOBAL_POSITION_INT`, `GPS_RAW_INT`, `SYSTEM_TIME`, `ATTITUDE_QUATERNION` and `COMMAND_ACK`;
- the TAP capture holds at least as many datagrams from and to each autopilot as the rig counted.

## The emulated TAP

[`tap.sh`](tap.sh) gives the rig a TAP monitor port, `ics-tap0`, as a capture machine cabled to a hardware TAP would see it. The kernel copies every frame each autopilot container sends or receives, using `nftables`' `dup` statement on the host side of the container's link, at both its ingress and egress hooks. The copies go into one end of a veth pair whose other end, `ics-tap0`, sits in its own network namespace. A capture there sees both directions.

The copy is passive: the autopilots' traffic is not delayed or changed. Nothing gets back out of the monitor port, because the host drops whatever arrives from it. The copies never reach the host's own network stack either, which would otherwise route them back onto the network. The capture is `tap.pcap`, which pcap capture ([ICS-020](https://github.com/MatthewK84/ICS/issues/20)) and the MAVLink adapter can replay.

## Addresses

The containers share one bridge network, `ics-range`, which exists only on the machine running the rig and is not a range network ([`compose.yaml`](compose.yaml)).

| Who | Address | MAVLink |
|---|---|---|
| The rig (`python -m ics_sitl`), on the host | 172.30.18.1 | listens on UDP 14551 for PX4 and 14550 for ArduPilot |
| PX4 v1.17.0, SIH quadrotor, system 1 | 172.30.18.10 | ground-station link on UDP 18570, sending to the rig |
| ArduCopter 4.7.1, quadrotor, system 2 | 172.30.18.20 | `SERIAL0`, sending to the rig |

## Files

| File | What it is |
|---|---|
| [`image/`](image) | The two autopilot images: Dockerfiles, the pinned sources ([`tools.txt`](image/tools.txt)), hash-pinned build requirements, and the start scripts. The workflow rebuilds them only when this folder changes. |
| [`image/px4-rc.mavlink`](image/px4-rc.mavlink) | PX4's MAVLink setup for the rig: one ground-station link whose partner is the rig. PX4's own setup takes the first sender after start-up as its partner for good, so a stray datagram at boot would cut the rig off. |
| [`engagements/`](engagements) | The scripted engagements |
| [`compose.yaml`](compose.yaml) | The two autopilot containers and their network |
| [`tap.sh`](tap.sh) | The emulated TAP, and the capture on it |
| [`run-engagements.sh`](run-engagements.sh) | Flies each engagement: containers, TAP, capture, the rig, then the TAP check |
| [`apt-packages.txt`](apt-packages.txt) | What the host needs: `iproute2`, `nftables` and `tcpdump` |

## Run it locally

You need Docker with Compose, Python 3.12 and root for `ip`, `nft` and `tcpdump` (the script uses `sudo` when not root). From the repository root:

```sh
docker build -f deploy/sitl/image/Dockerfile.px4 -t ics-sitl-px4:local .
docker build -f deploy/sitl/image/Dockerfile.ardupilot -t ics-sitl-ardupilot:local .
deploy/sitl/run-engagements.sh /tmp/sitl-records                                  # all three
deploy/sitl/run-engagements.sh /tmp/sitl-records deploy/sitl/engagements/crossing.toml
```

Behind a TLS-inspecting proxy, build with `--network host`, pass the proxy as build arguments and add `--secret id=extra_ca,src=<ca-bundle.pem>`, as for the [toolchain images](../toolchain/README.md).

Each engagement leaves a folder in the output directory:

| File | What it holds |
|---|---|
| `summary.json` | The verdict, any failures, the closest approach, each vehicle's progress, and the datagrams the rig counted on each link |
| `truth.jsonl` | One JSON object per line, timed from the start of the run: every position report, waypoint reached, autopilot text, refused command and phase change |
| `tap.pcap` | The capture on the TAP monitor port |
| `tap-counts.json` | The datagrams the capture holds from and to each autopilot |
| `containers.log` | Both autopilots' console output |
| `px4-log/` | PX4's onboard ULog log, in a folder for the day, as PX4 names it |
| `ardupilot-logs/` | ArduCopter's onboard DataFlash log (`00000001.BIN`) |
| `replay.jsonl` | In CI, the MAVLink replay of `tap.pcap` (below) |
| `time-align-fast.jsonl`, `time-align-slow.jsonl` | In CI, the time alignment checks of `tap.pcap`, with drift injected either way (below) |

## MAVLink replay

The nightly run then checks the MAVLink adapter ([ICS-021](https://github.com/MatthewK84/ICS/issues/21), [`cpp/mavlink`](../../cpp/README.md#mavlink)) against each engagement. `ics-mavlink-replay` runs the engagement's `tap.pcap` through the adapter, as `ics-plid` will run a TAP port, and `python -m ics_sitl compare` checks what came out against `truth.jsonl`. It passes when:

- every position the rig logged has a record from the same MAVLink system (PX4 is 1, ArduCopter 2) with the same `time_boot_ms`, latitude and longitude to 1e-9 degrees, and height above mean sea level to 1 mm, with the record's height above the ellipsoid no further from it than the geoid ever is;
- every status text and every refused command the rig logged is an event from that system, at least as often.

The replay runs in the `ics-cpp` image, which holds the EGM96 grid, with the engagements' range origin, 40 N, 100 W and 700 m above the ellipsoid. After a local run, with `ics-mavlink-replay` built in that image:

```sh
out=/tmp/sitl-records/crossing
docker run --rm -v "$PWD:/work/ics" -w /work/ics -v /tmp/sitl-records:/tmp/sitl-records ics-cpp:ci \
  cpp/build/gcc-release/testing/mavlink_replay/ics-mavlink-replay "$out/tap.pcap" 40 -100 700 > "$out/replay.jsonl"
PYTHONPATH=python python3 -m ics_sitl compare --truth "$out/truth.jsonl" --replay "$out/replay.jsonl"
```

## Time alignment

The nightly run then checks the drift fit ([ICS-026](https://github.com/MatthewK84/ICS/issues/26), [`cpp/timealign`](../../cpp/README.md#time-alignment)) against each engagement. `ics-time-align` runs twice. Each run puts 100 ppm of drift on every vehicle's boot clock, fast with a 5 s offset or slow with a 2 s one. It withholds the middle 60 % of each sortie's clock pairs, as a GNSS outage would, and fits the rest. The run fails unless the check applied to at least one sortie and, for each one, every position aligns within 1 ms of its reference.

The check's reference takes each clock to have no drift of its own, so it applies only to sorties whose offsets (UTC minus boot time) stay within 0.5 ms of their mean. ArduCopter's stay within 0.27 ms, and on each engagement every position aligns within 50 µs. PX4 SIH's stray more than a second: its boot clock is simulated time, about 2.5 % slower than the host clock that gives its UTC. So its sorties are reported as `drifting` and not checked.

```sh
docker run --rm -v "$PWD:/work/ics" -w /work/ics -v /tmp/sitl-records:/tmp/sitl-records ics-cpp:ci \
  cpp/build/gcc-release/testing/time_align/ics-time-align "$out/tap.pcap" 40 -100 700 --inject 100:5000 --withhold 0.2:0.8
```

## Onboard logs

`ics-log-import` ([ICS-025](https://github.com/MatthewK84/ICS/issues/25), [`cpp/flightlog`](../../cpp/README.md#onboard-logs)) imports either autopilot's onboard log and writes JSON lines, the same way:

```sh
docker run --rm -v "$PWD:/work/ics" -w /work/ics -v /tmp/sitl-records:/tmp/sitl-records ics-cpp:ci \
  cpp/build/gcc-release/testing/log_import/ics-log-import "$out/ardupilot-logs/00000001.BIN" 40 -100 700 2=target
```

PX4's built-in simulator gives its GNSS no UTC time, so PX4's log is untimed: its records carry only boot times. ArduCopter's log is timed by GPS week once its GNSS has a fix. The importer's sample logs are cut from these logs ([`cpp/flightlog/test/logs`](../../cpp/flightlog/test/logs/README.md)).

`ics-time-align` times both logs on the capture's clock instead, from each vehicle's `SYSTEM_TIME` pairs ([Time alignment](#time-alignment)):

```sh
docker run --rm -v "$PWD:/work/ics" -w /work/ics -v /tmp/sitl-records:/tmp/sitl-records ics-cpp:ci \
  cpp/build/gcc-release/testing/time_align/ics-time-align "$out/tap.pcap" 40 -100 700 \
  --log "$out/ardupilot-logs/00000001.BIN" --log "$out"/px4-log/*/*.ulg --records
```

## What to expect from the autopilots

Each start takes about a minute before the engagement begins. ArduCopter refuses to arm for about 40 seconds, until its estimator has a GPS position, and right after boot it has no mission storage yet. The rig resends every refused command and mission upload once a second until the engagement's time limit, and the truth log records each refusal. PX4 refuses a rate request for `MISSION_CURRENT`, which it already sends unasked, so rate requests are best effort; a stream that never arrives fails the verdict instead. PX4 also refuses `MAV_CMD_DO_SET_MISSION_CURRENT` as unsupported, which is why the rig uses the `MISSION_SET_CURRENT` message, and both autopilots accept that.

## Add an engagement

Copy an engagement file and change its points. Every key is required and no other key is allowed. Then check it loads, and fly it:

```sh
PYTHONPATH=python python3 -m ics_sitl env deploy/sitl/engagements/new.toml
deploy/sitl/run-engagements.sh /tmp/sitl-records deploy/sitl/engagements/new.toml
```

Plan the miss distance as a height difference where you can: if one vehicle starts its path a second later than the other, the height difference is still the closest approach.
