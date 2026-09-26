# ICS: Intercept Characterization System

> **Distribution: public.** ICS is developed from publicly available information only. Never commit CUI, CTI, export-controlled, classified or proprietary material. See [SECURITY.md](SECURITY.md) for what stays out of this repository and how to report a problem.

ICS produces truth-grade intercept, kill and debris data for non-explosive hit-to-kill counter-sUAS interceptors. It adapts the OWLSS stereo-optical tracking approach and fuses it with interceptor and target Position Location Information (PLI).

Start here:

- [Build plan](docs/build-plan.md): phases, language boundaries, coding standards, task card and Definition of Done.
- [GitHub issues (Path B)](docs/github-issues-path-b.md): the 97 issues, ICS-001 through ICS-097, by milestone.
- [Repository protection](docs/repository-protection.md): the rules on `main` and how changes get merged.

Work the issues in numeric order within each milestone. Open each one by completing its task card, and close it only when its "Done when" test passes and the Definition of Done checklist is complete. Every change reaches `main` through a pull request.

## Layout

| Directory | Contents | Language |
|---|---|---|
| [`proto/`](proto/) | Protobuf contracts, the single source of truth | Protobuf |
| [`schemas/`](schemas/) | Run-record JSON Schema keyed to C4 IDs | JSON Schema |
| [`golden/`](golden/) | Cross-language golden test vectors | Data |
| [`cpp/`](cpp/) | Real-time and production data paths and services | C++ |
| [`firmware/`](firmware/) | Encoder time-tagger firmware | C |
| [`python/`](python/) | Reference models, calibration, synthetic data, statistics | Python |
| [`web/`](web/) | Read-only operator and evaluator interface | TypeScript, React |
| [`deploy/`](deploy/) | Containers, Envoy, systemd units, infrastructure as code | Configuration |
| [`docs/`](docs/) | Build plan, issue index, repository guides | Markdown |
| [`planning/`](planning/) | Task manifest used to sync milestones | JSON |

Each directory's README lists its planned contents, its rules and the issue that first adds code there. Language boundaries follow the build plan: C++ owns every real-time path, Python never runs in the live capture or control path, and the web UI never commands instrumentation.
