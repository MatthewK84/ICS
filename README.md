<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/assets/readme/banner-dark.svg">
    <img alt="ICS: Intercept Characterization System. Two stations triangulate the closest approach of an interceptor and its target." src="docs/assets/readme/banner-light.svg" width="100%">
  </picture>
</p>

<p align="center">
  <a href="https://github.com/MatthewK84/ICS/actions/workflows/cpp-toolchain.yml"><img alt="C++ toolchain" src="https://github.com/MatthewK84/ICS/actions/workflows/cpp-toolchain.yml/badge.svg?branch=main"></a>
  <a href="https://github.com/MatthewK84/ICS/actions/workflows/python-toolchain.yml"><img alt="Python toolchain" src="https://github.com/MatthewK84/ICS/actions/workflows/python-toolchain.yml/badge.svg?branch=main"></a>
  <a href="https://github.com/MatthewK84/ICS/actions/workflows/web-toolchain.yml"><img alt="TypeScript toolchain" src="https://github.com/MatthewK84/ICS/actions/workflows/web-toolchain.yml/badge.svg?branch=main"></a>
  <a href="https://github.com/MatthewK84/ICS/actions/workflows/codeql.yml"><img alt="CodeQL" src="https://github.com/MatthewK84/ICS/actions/workflows/codeql.yml/badge.svg?branch=main"></a>
  <a href="https://github.com/MatthewK84/ICS/actions/workflows/dependency-register.yml"><img alt="Dependency register" src="https://github.com/MatthewK84/ICS/actions/workflows/dependency-register.yml/badge.svg?branch=main"></a>
</p>

<p align="center">
  <a href="#what-ics-does"><b>What it does</b></a> ·
  <a href="#architecture"><b>Architecture</b></a> ·
  <a href="#find-your-way"><b>Find your way</b></a> ·
  <a href="#repository-layout"><b>Layout</b></a> ·
  <a href="#quality-gates"><b>Quality gates</b></a> ·
  <a href="#quick-start"><b>Quick start</b></a> ·
  <a href="#contributing"><b>Contributing</b></a>
</p>

> [!IMPORTANT]
> **Distribution: public.** ICS is developed from publicly available information only. Never commit CUI, CTI, export-controlled, classified or proprietary material. [SECURITY.md](SECURITY.md) lists what stays out of this repository and how to report a problem.

## What ICS does

ICS produces truth-grade intercept, kill and debris data for non-explosive hit-to-kill counter-sUAS interceptors. It adapts the OWLSS stereo-optical tracking approach and fuses it with the interceptor's and the target's Position Location Information (PLI).

- **Every station times and keeps everything it sees.** Packets, PLI from four feeds, high-speed camera frames and mount encoder angles are each time-stamped in UTC nanoseconds, from PTP and GPS PPS.
- **Stations point themselves.** A cue from the target's PLI and radar tracks brings each mount onto the target, and a closed-loop tracker on the narrow-field camera holds it there.
- **The run becomes measurements** (Phase 4). Two or more stations triangulate the endgame. The products will report closest approach, contact time, debris and a kill classification, each traceable to its raw data.
- **Every result can be checked.** Each algorithm is modelled in Python first, and its C++ port is held to golden vectors. Run records will carry SHA-256 manifests and in-toto attestations.

## Architecture

<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/assets/readme/architecture-dark.svg">
    <img alt="How data moves through ICS. Sources (target PLI, radar tracks, high-speed cameras, the tracking camera and mount encoders) feed the ingest services (packet capture, PLI adapters, the time base, the camera service and the encoder time-tagger). Those feed the store and alignment stage, then station control (cue, tracker, rate loop, sun keep-out and mount), then the products (registration, detection, endgame, debris and kill class, and the run record shown in the web UI). Python reference models and golden vectors underpin station control and the products." src="docs/assets/readme/architecture-light.svg" width="100%">
  </picture>
</p>

Language boundaries follow the [build plan](docs/build-plan.md):
- C++ owns every real-time path.
- Python never runs in the live capture or control path.
- The web UI never commands instrumentation.

Positions are WGS84 and the range ENU frame, heights are EGM96 for mean sea level, and every time is UTC nanoseconds; [frames and time](docs/frames-and-time.md) defines them.

## Find your way

| I want to… | Start here |
|---|---|
| Understand the plan, its phases and the coding standards | [docs/build-plan.md](docs/build-plan.md) |
| See what is built and what comes next | [The issue index](docs/github-issues-path-b.md) and the [milestones](https://github.com/MatthewK84/ICS/milestones) |
| Change a message or a service contract | [proto/](proto/README.md) |
| Work on a real-time service, adapter or station control | [cpp/](cpp/README.md) and the [C++ module list](#c-modules) below |
| Write or change a reference model | [python/](python/README.md) |
| Add or regenerate golden test vectors | [golden/](golden/README.md) |
| Work on the operator and evaluator interface | [web/](web/README.md) |
| Build the images, benches and CI | [deploy/](deploy/README.md) and [deploy/toolchain/](deploy/toolchain/README.md) |
| Look up positions, heights and time | [docs/frames-and-time.md](docs/frames-and-time.md) |
| Follow the C++ rules, or record a deviation | [docs/cpp-jsf-av.md](docs/cpp-jsf-av.md) |
| Add a dependency | [docs/dependency-register.md](docs/dependency-register.md) |
| Use an AI assistant on a change | [docs/ai-usage.md](docs/ai-usage.md) |
| Report a security problem | [SECURITY.md](SECURITY.md) |

## Repository layout

<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/assets/readme/layout-dark.svg">
    <img alt="Repository layout. Contracts (proto, schemas and golden) feed the code (cpp in C++20, firmware in C, python and web in TypeScript). Operations live in deploy, docs and planning." src="docs/assets/readme/layout-light.svg" width="100%">
  </picture>
</p>

| Directory | Contents | Language |
|---|---|---|
| [`proto/`](proto/) | Protobuf contracts, the single source of truth | Protobuf |
| [`schemas/`](schemas/) | Run-record JSON Schema keyed to C4 IDs | JSON Schema |
| [`golden/`](golden/) | Cross-language golden test vectors | Data |
| [`cpp/`](cpp/) | Real-time and production data paths and services | C++20 |
| [`firmware/`](firmware/) | Encoder time-tagger firmware | C |
| [`python/`](python/) | Reference models, calibration, synthetic data, statistics | Python |
| [`web/`](web/) | Read-only operator and evaluator interface | TypeScript, React |
| [`deploy/`](deploy/) | Containers, CI images, benches, systemd units | Configuration |
| [`docs/`](docs/) | Build plan, issue index, standards, repository guides | Markdown |
| [`planning/`](planning/) | Task manifest used to sync milestones | JSON |

Each directory's README lists its contents, its rules and the issues that added them.

<details id="c-modules">
<summary><b>C++ modules</b>: 21 modules, each linked to its section of <a href="cpp/README.md">cpp/README.md</a></summary>
<br>

| Module | Directory | What it does |
|---|---|---|
| [Common library](cpp/README.md#common-library) | `common/` | Results, units, checks and fixed-capacity containers that every real-time component links |
| [Logging](cpp/README.md#logging) | `logging/` | Structured logging, set up once per service |
| [Config](cpp/README.md#config) | `config/` | TOML configuration, read against a schema written as code |
| [Frames](cpp/README.md#frames) | `frames/` | Conversions between WGS84, the range ENU frame and EGM96 heights |
| [Timing](cpp/README.md#timing) | `timing/`, `services/timingd/` | The station's PTP time quality, served by `ics-timingd` |
| [Capture](cpp/README.md#capture) | `capture/`, `services/capd/` | Every packet on the station's TAP ports, captured by `ics-capd` |
| [MAVLink](cpp/README.md#mavlink) | `mavlink/` | PLI adapter for MAVLink |
| [Cursor on Target](cpp/README.md#cursor-on-target) | `cot/` | PLI adapter for CoT XML |
| [Lattice](cpp/README.md#lattice) | `lattice/` | PLI adapter for Lattice entity streams |
| [SAPIENT](cpp/README.md#sapient) | `sapient/` | Radar-track adapter for BSI Flex 335 |
| [Onboard logs](cpp/README.md#onboard-logs) | `flightlog/` | Import of PX4 ULog and ArduPilot DataFlash logs |
| [Time alignment](cpp/README.md#time-alignment) | `timealign/` | Each vehicle's clock fitted to UTC after a sortie |
| [PLI store](cpp/README.md#pli-store) | `store/`, `services/plid/` | Segment log and Parquet store, served by `ics-plid` |
| [Cameras](cpp/README.md#cameras) | `camera/` | Phantom and FLIR high-speed camera drivers and offload |
| [Camera service](cpp/README.md#camera-service) | `services/camd/` | `ics-camd`: one command arms and offloads every camera |
| [Strobe calibration](cpp/README.md#strobe-calibration) | `strobe/` | Each camera's frame-time offset, measured with a strobe |
| [Mount client](cpp/README.md#mount-client) | `mount/` | PWI4 client, rate loop and sun keep-out interlock |
| [Encoder time-tagger](cpp/README.md#encoder-time-tagger) | `tagger/` | The time-tagger's core, shared with the firmware |
| [Encoder module](cpp/README.md#encoder-module) | `encoder/` | Each camera frame's line of sight from encoder angles |
| [Cue extrapolator](cpp/README.md#cue-extrapolator) | `cue/` | PLI and radar fused into each station's pointing cue |
| [Tracker](cpp/README.md#tracker) | `tracker/` | Closed-loop tracking on the narrow-field camera, through Aravis |

</details>

## Quality gates

Every change reaches `main` through a pull request, and CI enforces each gate below.

| Gate | What it holds | Details |
|---|---|---|
| Power of Ten | Warnings as errors, a strict clang-tidy profile and no suppression markers in any language | [C++](cpp/README.md#power-of-ten-checks), [Python](python/README.md#power-of-ten-checks), [web](web/README.md#power-of-ten-checks) |
| JSF AV C++ | The JSF AV rules on every new C++ module, with each deviation registered | [docs/cpp-jsf-av.md](docs/cpp-jsf-av.md) |
| Sanitizers | ASan, UBSan and TSan on every C++ change | [Test stages](cpp/README.md#test-stages) |
| Coverage | Every line and branch of gated C++; at least 90% of Python and 80% of web code | [coverage-gates.txt](cpp/policy/coverage-gates.txt) |
| Golden parity | Each C++ port matches its Python reference | [golden/](golden/README.md) |
| Reproducible builds | Two GCC builds give byte-identical outputs | [Build](cpp/README.md#build) |
| CodeQL | Fails on any finding | [codeql.yml](.github/workflows/codeql.yml) |
| Dependencies | Every dependency pinned and reviewed for license, sustainment and foreign influence | [Dependency register](docs/dependency-register.md) |
| Evidence | A signed SBOM and provenance for every merge | [deploy/evidence/](deploy/evidence/README.md) |

## Quick start

Each toolchain runs in a pinned environment; its README has the full detail.

<details>
<summary><b>C++</b>: build and test inside the <code>ics-cpp</code> image</summary>

```sh
deploy/toolchain/conan-install.sh gcc-release   # dependencies from cpp/conan.lock
cd cpp
cmake --preset gcc-release
cmake --build --preset gcc-release
ctest --preset gcc-release
```

Presets pair `gcc` or `clang` with `debug`, `release`, `asan`, `ubsan` or `tsan`. See [cpp/README.md](cpp/README.md#build).

</details>

<details>
<summary><b>Python</b>: reference models, with uv pinned by hash</summary>

```sh
cd python
python3.12 -m venv /tmp/uv && /tmp/uv/bin/pip install --require-hashes --no-deps -r requirements-uv.txt
export PATH="/tmp/uv/bin:${PATH}"
uv sync --locked
uv run --locked ruff check . && uv run --locked mypy .
uv run --locked pytest
```

See [python/README.md](python/README.md#toolchain).

</details>

<details>
<summary><b>TypeScript</b>: the web workspace, with pnpm pinned</summary>

```sh
cd web
corepack enable && pnpm install --frozen-lockfile
pnpm run format:check && pnpm run lint && pnpm run typecheck
pnpm run test
```

See [web/README.md](web/README.md#toolchain).

</details>

## Contributing

Work the issues in numeric order within each milestone.
1. Open each issue by completing its task card: the goal, blocking questions, assumptions and plan.
2. Build it on a branch, and bring it to `main` in a pull request.
3. Close the issue only when its "Done when" test passes and the Definition of Done is complete.

- [Contributing](CONTRIBUTING.md): the workflow, task card, Definition of Done, branch naming and review rules.
- [Repository protection](docs/repository-protection.md): the rules on `main` and how changes get merged.
- [AI usage](docs/ai-usage.md): how AI-assisted changes are declared and reviewed.
- [Security](SECURITY.md): what never enters this repository, and how to report a problem.
