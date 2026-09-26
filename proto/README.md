# proto

Protobuf contracts, the single source of truth for every message that crosses a process or language boundary. C++ uses the generated types over gRPC, the browser reaches `ics-api` through Envoy gRPC-Web, and Python reads the same types for golden files and analysis.

**Language:** Protobuf. **Lead role:** Architect.

Planned messages: `TimeQuality`, `PliRecord`, `PliEvent`, `MountSample`, `CameraFrameMeta`, `TriggerEvent`, `Track`, `Fragment`, `KillAssessment`, `Footprint`, `RunRecord`.

Rules:

- Document every field with its units.
- `buf lint` and `buf breaking` must pass; changes are additive.
- Frames and time follow the conventions fixed in [ICS-014](https://github.com/MatthewK84/ICS/issues/14): WGS84, range ENU origin at the defended asset, UTC as int64 nanoseconds, ellipsoid heights.

First issues: [ICS-011](https://github.com/MatthewK84/ICS/issues/11) (tooling), [ICS-012](https://github.com/MatthewK84/ICS/issues/12) (messages).
