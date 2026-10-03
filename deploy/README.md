# deploy

Containers, the Envoy gRPC-Web proxy, systemd units and infrastructure as code for the field server, station computers and CI toolchain images.

**Language:** Dockerfiles, Envoy and systemd configuration, infrastructure as code. **Lead role:** DevSecOps.

Rules:

- Pin every base image and dependency; lockfiles are committed.
- Every build produces a CycloneDX SBOM and signed provenance ([ICS-009](https://github.com/MatthewK84/ICS/issues/9), [`evidence/`](evidence/README.md)).
- No credentials, range network addresses or other sensitive configuration in this repository; see [SECURITY.md](../SECURITY.md).

Contents:

- [`toolchain/`](toolchain/README.md): the C++ and CUDA build images ([ICS-004](https://github.com/MatthewK84/ICS/issues/4)).
- [`evidence/`](evidence/README.md): the signed SBOMs and provenance published for every merge and image ([ICS-009](https://github.com/MatthewK84/ICS/issues/9)).
- [`sitl/`](sitl/README.md): the SITL rig, PX4 and ArduPilot simulators flying scripted engagements with their MAVLink mirrored to an emulated TAP port ([ICS-018](https://github.com/MatthewK84/ICS/issues/18)). Test equipment: its images are built in CI with SBOMs and never published.
- [`timing-bench/`](timing-bench/README.md): the timing bench, two `ptp4l` instances in network namespaces that check `ics-timingd` flags holdover within 1 s of emulated GNSS loss, and the same check on real hardware ([ICS-019](https://github.com/MatthewK84/ICS/issues/19)). Test equipment: nothing in it ships.
- [`capture-bench/`](capture-bench/README.md): the capture bench, a UDP sender and `ics-capd` in network namespaces on a veth pair, which checks an accelerated 24 h capture with zero drops, every packet in the files and every hash matching, and the 24 h run on real hardware ([ICS-020](https://github.com/MatthewK84/ICS/issues/20)). Test equipment: nothing in it ships.
