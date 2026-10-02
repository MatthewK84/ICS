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

## What to expect from the autopilots

Each start takes about a minute before the engagement begins. ArduCopter refuses to arm for about 40 seconds, until its estimator has a GPS position, and right after boot it has no mission storage yet. The rig resends every refused command and mission upload once a second until the engagement's time limit, and the truth log records each refusal. PX4 refuses a rate request for `MISSION_CURRENT`, which it already sends unasked, so rate requests are best effort; a stream that never arrives fails the verdict instead. PX4 also refuses `MAV_CMD_DO_SET_MISSION_CURRENT` as unsupported, which is why the rig uses the `MISSION_SET_CURRENT` message, and both autopilots accept that.

## Add an engagement

Copy an engagement file and change its points. Every key is required and no other key is allowed. Then check it loads, and fly it:

```sh
PYTHONPATH=python python3 -m ics_sitl env deploy/sitl/engagements/new.toml
deploy/sitl/run-engagements.sh /tmp/sitl-records deploy/sitl/engagements/new.toml
```

Plan the miss distance as a height difference where you can: if one vehicle starts its path a second later than the other, the height difference is still the closest approach.
