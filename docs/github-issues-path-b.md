# ICS GitHub Issues: Full Re-implementation (Path B)

Sep 26, 2026 · @Matthew Kolakowski

## BLUF

Create 97 GitHub issues, ICS-001 through ICS-097, and work them in numeric order within each milestone. Direct task effort totals about 625 person-weeks; reviews, management and field support fill the rest of the 16 engineer-years. Path B adds 14 issues that replace the OWLSS algorithms: Python references for detection and association, plus full C++ registration, detection, association and estimation. Each issue lists its plan ID from the [Build plan](build-plan.md), its dependencies, its steps and the test that closes it.

## Repository setup

Create these labels and milestones before loading any issue.

| Type | Values |
|---|---|
| Milestones | M0 Foundation, M1 Core services, M2 Station control, M3 Python models, M4 Algorithms, M5 Front end, M6 Integration and release |
| Language labels | lang:cpp, lang:python, lang:ts, lang:fw, lang:proto, lang:all |
| Area labels | area:timing, area:pli, area:camera, area:mount, area:registration, area:detection, area:association, area:estimation, area:products, area:ui, area:validation, area:devsecops |
| Path label | path-b on every issue that exists only because OWLSS algorithms are re-implemented |
| Risk label | critical-path on ICS-032, 035, 036, 059, 067, 094, 096 |

**Conventions**

- Title each issue `ICS-### Verb object`, exactly as listed below.
- Paste the task card into the issue body and complete it before coding starts.
- Name branches `ics-###-short-slug`; each pull request closes one issue.
- Link dependencies with GitHub's "blocked by" relationship.
- Close an issue only when its "Done when" test passes and the Definition of Done checklist is complete.
- Read "Weeks" as estimated person-weeks of direct effort.

## M0 Foundation and contracts

Build the repository, toolchains, evidence pipeline and shared contracts. No product code merges until M0 closes.

| # | Issue (plan ID) | Weeks | Blocked by | Steps | Done when |
|---|---|---|---|---|---|
| [ICS-001](https://github.com/MatthewK84/ICS/issues/1) | Create the monorepo and protections (FND-01) | 1 | None | 1) Create ics on the CUI-authorized host with the Build plan layout. 2) Add CODEOWNERS per top-level directory. 3) Protect main: two approvals, signed commits, linear history. 4) Add the CTI banner and SECURITY.md. 5) Create labels and milestones. | Unsigned or unreviewed pushes to main fail |
| [ICS-002](https://github.com/MatthewK84/ICS/issues/2) | Add CONTRIBUTING, task card and Definition of Done (FND-09) | 1 | 001 | 1) Write CONTRIBUTING with the task card and Definition of Done. 2) Add issue and pull-request templates. 3) Document branch naming. | Templates render on new issues and pull requests |
| [ICS-003](https://github.com/MatthewK84/ICS/issues/3) | Add AI-assist controls per DoWI 8430.01 §3.6 (FND-08) | 1 | 002 | 1) Add an ai-assisted checkbox to the pull-request template. 2) Require a second human approver when checked. 3) Log model, version and scope in docs/ai-usage.md. | CI blocks merge until the checkbox is answered |
| [ICS-004](https://github.com/MatthewK84/ICS/issues/4) | Build C++ toolchain containers (FND-02) | 3 | 001 | 1) Build images with GCC 13, Clang 17 and CUDA 12. 2) Add CMake presets for debug, release, asan and tsan. 3) Pin dependencies with a Conan 2 lockfile. | A sample target builds bit-identically twice on both compilers |
| [ICS-005](https://github.com/MatthewK84/ICS/issues/5) | Encode the C++ Power-of-Ten profile (FND-03) | 3 | 004 | 1) Write .clang-tidy with the rule-table checks. 2) Set -Wall -Wextra -Wpedantic -Wconversion -Werror. 3) Reject NOLINT in CI. 4) Add a malloc hook that fails tests on post-init allocation. | CI fails on one seeded violation of each rule |
| [ICS-006](https://github.com/MatthewK84/ICS/issues/6) | Build the Python toolchain (FND-04) | 1 | 001 | 1) Pin Python 3.12 and dependencies with uv. 2) Configure ruff, mypy --strict, pytest and hypothesis. 3) Add AST checks for recursion and function length. | CI fails on an untyped or recursive function |
| [ICS-007](https://github.com/MatthewK84/ICS/issues/7) | Build the TypeScript toolchain (FND-05) | 1 | 001 | 1) Create a pnpm workspace with Vite and React 18. 2) Enable strict tsconfig. 3) Configure ESLint: no any, no non-null assertion, no floating promises, 50-line functions. 4) Add Vitest and Playwright. | CI fails on any, !, var or a floating promise |
| [ICS-008](https://github.com/MatthewK84/ICS/issues/8) | Build CI test stages (FND-06) | 3 | 004 to 007 | 1) Add build and unit stages for all languages. 2) Add ASan, UBSan and TSan builds. 3) Add libFuzzer runs per merge and nightly. 4) Add clang-tidy, cppcheck, CodeQL, ruff, mypy and ESLint. | Seeded defects fail each stage; clean code passes |
| [ICS-009](https://github.com/MatthewK84/ICS/issues/9) | Build CI evidence stages (FND-06) | 3 | 008 | 1) Generate CycloneDX SBOMs with Syft for Conan, uv and pnpm. 2) Sign artifacts with cosign. 3) Emit in-toto provenance. 4) Archive evidence per merge. | Every merge publishes a signed SBOM and attestation |
| [ICS-010](https://github.com/MatthewK84/ICS/issues/10) | Open the dependency register per DoWI 8430.01 §3.5.i (FND-07) | 3 | 009 | 1) List every direct dependency. 2) Record governance, sustainment, license and foreign-influence review. 3) Flag single-maintainer projects. 4) Fail CI on unregistered dependencies. | CI rejects a new unregistered dependency |
| [ICS-011](https://github.com/MatthewK84/ICS/issues/11) | Set up protobuf tooling (PRO-01) | 2 | 004, 006, 007 | 1) Add buf with lint and breaking-change checks. 2) Generate C++, Python and TypeScript code. 3) Publish generated packages in the monorepo. | buf generate runs in CI for all three languages |
| [ICS-012](https://github.com/MatthewK84/ICS/issues/12) | Define ICS protobuf messages (PRO-01) | 4 | 011 | 1) Define TimeQuality, PliRecord and PliEvent. 2) Define MountSample, CameraFrameMeta and TriggerEvent. 3) Define Track, Fragment, KillAssessment, Footprint and RunRecord. 4) Document every field with units. | buf lint passes; all three language leads approve |
| [ICS-013](https://github.com/MatthewK84/ICS/issues/13) | Publish the run-record JSON Schema and DroneScore mapping (PRO-02) | 3 | 012 | 1) Write the JSON Schema keyed to C4 MOP and KPP IDs. 2) Build 20 sample records. 3) Map fields to the DroneScore import. | All 20 samples validate and import into DroneScore |
| [ICS-014](https://github.com/MatthewK84/ICS/issues/14) | Fix frame and time conventions (PRO-03) | 2 | 012 | 1) Document WGS84, range ENU origin, UTC as int64 ns and ellipsoid heights. 2) Generate golden conversion vectors with GeographicLib. | Golden vectors merged into golden/ |

## M1 Core services: timing, PLI and camera I/O

Build the C++ foundation, the SITL rig and every data-ingest path. All issues are `lang:cpp`.

| # | Issue (plan ID) | Weeks | Blocked by | Steps | Done when |
|---|---|---|---|---|---|
| [ICS-015](https://github.com/MatthewK84/ICS/issues/15) | Build common error and container types (CPP-01) | 5 | 005 | 1) Wrap tl::expected with an ICS error enum. 2) Build StaticVector, RingBuffer and fixed pools. 3) Add assertion macros that CI counts. | Full branch coverage; zero post-init allocations |
| [ICS-016](https://github.com/MatthewK84/ICS/issues/16) | Build common units, logging and config (CPP-01) | 4 | 015 | 1) Add strong unit types for time, angle and length. 2) Wrap spdlog with structured fields. 3) Load TOML config with toml++ and schema checks. | Invalid config fails fast and names the field |
| [ICS-017](https://github.com/MatthewK84/ICS/issues/17) | Build common frames (CPP-02) | 3 | 014, 016 | 1) Implement WGS84, ECEF and ENU transforms. 2) Convert MSL to ellipsoid with EGM96. 3) Test against golden vectors. | Every golden vector matches within 1 mm |
| [ICS-018](https://github.com/MatthewK84/ICS/issues/18) | Stand up the SITL rig (INT-01) | 4 | 008 | 1) Containerize PX4 and ArduPilot SITL. 2) Script target and interceptor profiles. 3) Mirror MAVLink to an emulated TAP port. | Nightly CI replays three scripted engagements |
| [ICS-019](https://github.com/MatthewK84/ICS/issues/19) | Build ics-timingd (CPP-03) | 5 | 016 | 1) Poll grandmaster and PTP state. 2) Detect GNSS loss and holdover. 3) Publish TimeQuality over gRPC. | Holdover flagged within 1 s of GNSS loss on the bench |
| [ICS-020](https://github.com/MatthewK84/ICS/issues/20) | Capture pcap on TAP ports (CPP-05) | 4 | 016 | 1) Open TAP interfaces with libpcap. 2) Enable NIC hardware time stamps. 3) Rotate files and hash each on close. | 24 h capture with zero drops |
| [ICS-021](https://github.com/MatthewK84/ICS/issues/21) | Build the MAVLink adapter (CPP-06) | 12 | 017, 018, 020 | 1) Parse GLOBAL_POSITION_INT, GPS_RAW_INT, SYSTEM_TIME, ATTITUDE_QUATERNION, HEARTBEAT, COMMAND_LONG/ACK and STATUSTEXT. 2) Convert MSL to ellipsoid. 3) Emit PliRecord and PliEvent. 4) Add a libFuzzer target. | SITL replays match truth; 1 h fuzz clean |
| [ICS-022](https://github.com/MatthewK84/ICS/issues/22) | Build the CoT adapter (CPP-07) | 4 | 017, 020 | 1) Parse event and point with pugixml. 2) Map ce and le to σ. 3) Flag receipt time versus vehicle time. 4) Fuzz. | Sample feeds parse; fuzz clean |
| [ICS-023](https://github.com/MatthewK84/ICS/issues/23) | Build the Lattice adapter (CPP-08) | 6 | 017 | 1) Subscribe to StreamEntities with a read-only token. 2) Filter location and kinematics components. 3) Emit PliRecord. | Runs against the vendor sandbox |
| [ICS-024](https://github.com/MatthewK84/ICS/issues/24) | Build the SAPIENT adapter (CPP-09) | 6 | 017 | 1) Generate BSI Flex 335 v2.0 types. 2) Parse detection and track reports. 3) Fuzz. | ICD sample messages parse; fuzz clean |
| [ICS-025](https://github.com/MatthewK84/ICS/issues/25) | Import ULog and DataFlash logs (CPP-10) | 10 | 017 | 1) Parse ULog with ulog_cpp. 2) Parse DataFlash. 3) Extract estimator states, raw GNSS and events. 4) Convert GNSS time to UTC. | Full-rate states extracted from sample logs |
| [ICS-026](https://github.com/MatthewK84/ICS/issues/26) | Build time alignment (CPP-11) | 8 | 021, 025 | 1) Fit boot-to-UTC offset and drift per sortie. 2) Compute link latency per source. 3) Report residual offsets. | ≤1 ms error with injected drift on SITL |
| [ICS-027](https://github.com/MatthewK84/ICS/issues/27) | Wrap the Phantom SDK (CPP-12) | 10 | 016 | 1) Configure resolution, rate and exposure. 2) Arm multi-cine segments and trigger. 3) Trim and offload over 10GbE. 4) Extract IRIG time per frame. | Ten back-to-back segments offload with verified times |
| [ICS-028](https://github.com/MatthewK84/ICS/issues/28) | Wrap the FLIR X6980-HS SDK (CPP-13) | 6 | 016 | 1) Configure window, rate and integration time. 2) Arm and trigger. 3) Offload with IRIG time stamps. | Same test as ICS-027 |
| [ICS-029](https://github.com/MatthewK84/ICS/issues/29) | Build the strobe latency analyzer (CPP-04) | 4 | 019, 027 | 1) Detect PPS strobe frames. 2) Compute per-camera offset to UTC. 3) Publish offsets to TimeQuality. | Offsets within 5 µs on the bench |
| [ICS-030](https://github.com/MatthewK84/ICS/issues/30) | Deliver ics-plid (CPP-14) | 6 | 021 to 026 | 1) Wire all adapters into one service. 2) Store canonical PLI as Parquet. 3) Expose gRPC queries. | SITL feed yields canonical PLI end to end |
| [ICS-031](https://github.com/MatthewK84/ICS/issues/31) | Deliver ics-camd (CPP-14) | 6 | 027, 028 | 1) Manage every station camera. 2) Arm and offload on one command. 3) Record frame metadata. | One command arms and offloads every camera |

## M2 Station control

Build the encoder time-tagger, mount control, tracking and triggering. These issues sit on the critical path; staff ICS-032 and ICS-035 in month 1.

| # | Issue (plan ID) | Weeks | Blocked by | Steps | Done when |
|---|---|---|---|---|---|
| [ICS-032](https://github.com/MatthewK84/ICS/issues/32) | Build the encoder time-tagger firmware (FW-01), lang:fw | 12 | 014 | 1) Select the MCU or FPGA and the encoder tap interface. 2) Discipline the clock to PPS and PTP. 3) Stream time-tagged angles. 4) Test with injected PPS. | Time-tag error ≤50 µs (C18) |
| [ICS-033](https://github.com/MatthewK84/ICS/issues/33) | Build the PWI4 mount client (CPP-16) | 6 | 016 | 1) Implement status, goto and rate commands. 2) Enforce axis limits. 3) Retry with bounded backoff. | Stable 20 to 50 Hz rate loop on a bench mount |
| [ICS-034](https://github.com/MatthewK84/ICS/issues/34) | Build the sun keep-out interlock (CPP-17) | 5 | 033 | 1) Compute sun position with NREL SPA. 2) Pre-empt any command entering the 15° cone. 3) Park the mount on ephemeris or clock fault. | Randomized HIL commands never enter the cone |
| [ICS-035](https://github.com/MatthewK84/ICS/issues/35) | Fit the mount pointing model (PY-01), lang:python | 6 | 014, 032 | 1) Capture star calibration frames. 2) Fit TPOINT-style terms. 3) Export versioned coefficients. | ≤10 µrad RMS on held-out stars (C17) |
| [ICS-036](https://github.com/MatthewK84/ICS/issues/36) | Build the encoder module (CPP-15) | 6 | 032, 035 | 1) Ingest time-tagged angles. 2) Interpolate to frame times. 3) Apply the pointing model. | ≤5 µrad error at 0.1 rad/s on the bench |
| [ICS-037](https://github.com/MatthewK84/ICS/issues/37) | Build the cue extrapolator (CPP-18) | 8 | 026, 033 | 1) Fuse PLI and radar in a constant-acceleration Kalman filter. 2) Convert to azimuth, elevation and rates. 3) Compensate latency. | Pointing error <2 mrad on replayed tracks |
| [ICS-038](https://github.com/MatthewK84/ICS/issues/38) | Build the closed-loop tracker (CPP-19) | 12 | 033 | 1) Stream the tracking camera through Aravis. 2) Centroid the target at 100 Hz. 3) Close the loop to mount rates. 4) Add acquire and reacquire logic. | Target stays in the central half of the narrow field at 500 m (C19) |
| [ICS-039](https://github.com/MatthewK84/ICS/issues/39) | Build the hand-off scheduler (CPP-20) | 4 | 037, 038 | 1) Queue targets by predicted engagement time. 2) Re-point after each engagement. 3) Flag engagements less than 3 s apart. | M-2 replay hands off within 3 s |
| [ICS-040](https://github.com/MatthewK84/ICS/issues/40) | Build the trigger controller (CPP-21) | 8 | 026, 031 | 1) Predict closest approach from PLI. 2) Arm on the engage command. 3) Drive the BNC 577 over SCPI. 4) Log every event on UTC. | Fires within 1 ms of prediction in replay |
| [ICS-041](https://github.com/MatthewK84/ICS/issues/41) | Deliver ics-mountd and the station console (CPP-22) | 6 | 034, 036 to 039 | 1) Package control, interlock and tracking into one service per station. 2) Add a station console CLI for operator commands. | Three stations track one target from one cue |

## M3 Python models

Write every algorithm first as a reviewed Python reference with golden outputs; the C++ ports in M4 must match them. All issues are `lang:python`; ICS-048 and ICS-049 also carry `path-b`.

| # | Issue (plan ID) | Weeks | Blocked by | Steps | Done when |
|---|---|---|---|---|---|
| [ICS-042](https://github.com/MatthewK84/ICS/issues/42) | Fit camera intrinsics and boresight (PY-02) | 6 | 014 | 1) Capture calibration targets. 2) Fit Brown-Conrady distortion. 3) Fit narrow, wide and MWIR boresight offsets to the mount. 4) Export parameters. | ≤0.5 px reprojection on held-out points (C3) |
| [ICS-043](https://github.com/MatthewK84/ICS/issues/43) | Model refraction and turbulence (PY-03) | 6 | 042 | 1) Compute terrestrial refraction from met profiles. 2) Estimate turbulence from image jitter. 3) Export correction tables. | Reduces RTK residuals on V2 data |
| [ICS-044](https://github.com/MatthewK84/ICS/issues/44) | Write the 7-state ballistic reference (PY-04) | 5 | 014 | 1) Implement the point-mass drag model. 2) Fit initial state and ρ/β. 3) Publish golden datasets. | Recovers synthetic truth; golden files merged |
| [ICS-045](https://github.com/MatthewK84/ICS/issues/45) | Write lift and flutter references (PY-04) | 6 | 044 | 1) Add lift and velocity-dependent drag models. 2) Add model selection by residual. 3) Publish golden files. | Recovers synthetic parameters within tolerance |
| [ICS-046](https://github.com/MatthewK84/ICS/issues/46) | Write IMM and closest-approach references (PY-04) | 6 | 044 | 1) Implement IMM with constant-velocity, constant-acceleration and turn models. 2) Solve closest approach and contact time. 3) Publish golden files. | Miss distance within 1 mm on synthetic truth |
| [ICS-047](https://github.com/MatthewK84/ICS/issues/47) | Build the synthetic engagement generator (PY-08) | 12 | 012 | 1) Render sky backgrounds with noise and turbulence. 2) Insert drones, interceptor and debris at known truth. 3) Write cine-compatible frames with truth files. | 200-sequence regression set runs in CI |
| [ICS-048](https://github.com/MatthewK84/ICS/issues/48) | Write the reference detector, path-b | 6 | 047 | 1) Model the sky background on moving frames. 2) Detect negative- and positive-contrast blobs. 3) Measure sub-pixel centroid and area. 4) Publish golden outputs. | ≥95% recall at ≤1 false detection per frame on synthetic data |
| [ICS-049](https://github.com/MatthewK84/ICS/issues/49) | Write the reference debris association, path-b | 10 | 047, 048 | 1) Build 2D tracklets. 2) Match across stations by line-of-sight gating. 3) Manage track hypotheses. 4) Publish golden outputs. | ≥90% correct associations on synthetic debris clouds |
| [ICS-050](https://github.com/MatthewK84/ICS/issues/50) | Build drag-table fitting tools (PY-05) | 4 | 044 | 1) Ingest recovery weights and tracks. 2) Fit Cd by material class. 3) Hold out half of every drop set. | Runs on synthetic drops; real fit follows ICS-095 |
| [ICS-051](https://github.com/MatthewK84/ICS/issues/51) | Write the footprint Monte Carlo reference (PY-06) | 6 | 044 | 1) Ingest wind profiles to 1,000 m. 2) Propagate fragments with sampled uncertainty. 3) Produce 50, 90 and 99% regions. | Coverage test passes on synthetic truth |
| [ICS-052](https://github.com/MatthewK84/ICS/issues/52) | Study kill-class thresholds (PY-07) | 4 | 044 | 1) Build ROC curves for N, m, T and the residual gate. 2) Recommend thresholds. 3) Freeze them for Section 5.2 sign-off. | Thresholds signed before scoring runs |
| [ICS-053](https://github.com/MatthewK84/ICS/issues/53) | Train the segmentation model (PY-09) | 10 | 047 | 1) Label range and synthetic imagery. 2) Train and benchmark. 3) Export ONNX with a model card (DoWI 8430.01 §3.6.e). | IoU ≥0.8; model card published |
| [ICS-054](https://github.com/MatthewK84/ICS/issues/54) | Propagate uncertainty (PY-10) | 5 | 035, 042, 043 | 1) Sample every error source. 2) Predict σ versus slant range. 3) Compare with Section 4.2. | Within 20% of the Section 4.2 table |
| [ICS-055](https://github.com/MatthewK84/ICS/issues/55) | Build the statistics package (PY-11) | 5 | 013 | 1) Compute Clopper-Pearson Pk. 2) Summarize miss distance and timeline gates. 3) Compute MLCOA versus MDCOA deltas. | Reproduces hand-worked examples |
| [ICS-056](https://github.com/MatthewK84/ICS/issues/56) | Build the validation scorer (PY-12) | 6 | 055 | 1) Load V0 to V5 data. 2) Compute C1 to C19. 3) Emit the accreditation table. | One command produces the table |
| [ICS-057](https://github.com/MatthewK84/ICS/issues/57) | Build the debris source-term model (PY-13) | 4 | 050 | 1) Parameterize by closing speed, geometry and target class. 2) Export M&S tables. | M&S Analyst accepts the tables |

## M4 Algorithms, fully re-implemented

Port each Python reference to C++ and gate every port on parity with its golden files. All issues are `lang:cpp`; issues marked `path-b` replace OWLSS algorithms.

### Registration

| # | Issue (plan ID) | Weeks | Blocked by | Steps | Done when |
|---|---|---|---|---|---|
| [ICS-058](https://github.com/MatthewK84/ICS/issues/58) | Build the camera model, path-b (CPP-23) | 6 | 042 | 1) Load intrinsics and distortion. 2) Undistort and project points. 3) Test parity with ICS-042. | Parity within 0.01 px |
| [ICS-059](https://github.com/MatthewK84/ICS/issues/59) | Build per-frame lines of sight (CPP-23) | 8 | 036, 058 | 1) Combine encoder angle, pointing model and boresight per frame. 2) Output unit vectors in range ENU. | Sky-fiducial error meets C4 and C5 on V2 data |
| [ICS-060](https://github.com/MatthewK84/ICS/issues/60) | Apply refraction corrections (CPP-23) | 4 | 043, 059 | 1) Load the ICS-043 tables. 2) Correct each line of sight by elevation and range. | Parity with ICS-043 |
| [ICS-061](https://github.com/MatthewK84/ICS/issues/61) | Build multi-station triangulation, path-b (CPP-23) | 8 | 059 | 1) Solve least-squares intersection with covariance. 2) Reject pairs outside 40 to 140° intersection. | Matches RTK within C4 and C5 |

### Detection

| # | Issue (plan ID) | Weeks | Blocked by | Steps | Done when |
|---|---|---|---|---|---|
| [ICS-062](https://github.com/MatthewK84/ICS/issues/62) | Build GPU frame ingest, path-b (CPP-24) | 8 | 027 | 1) Decode cine frames into device memory. 2) Apply dark and flat correction. 3) Stream frame batches. | A 1.5 s, 5,000 fps window processes in ≤60 s |
| [ICS-063](https://github.com/MatthewK84/ICS/issues/63) | Build sky-background detection on moving frames, path-b (CPP-24) | 14 | 048, 062 | 1) Model background per frame. 2) Detect negative and positive contrast. 3) Test parity with ICS-048. | ≥95% recall at ≤1 false detection per frame |
| [ICS-064](https://github.com/MatthewK84/ICS/issues/64) | Measure blobs, path-b (CPP-24) | 8 | 063 | 1) Label connected components on the GPU. 2) Compute sub-pixel centroids and projected areas. | Centroid error ≤0.3 px on synthetic truth |
| [ICS-065](https://github.com/MatthewK84/ICS/issues/65) | Build MWIR hot-object detection (CPP-24) | 6 | 062 | 1) Detect hot objects. 2) Reuse blob measurement. | ≥90% recall on the night synthetic set |
| [ICS-066](https://github.com/MatthewK84/ICS/issues/66) | Run segmentation through TensorRT (CPP-25) | 5 | 053, 062 | 1) Build a TensorRT engine from ONNX. 2) Run inference per frame. | ONNX parity; ≤5 ms per frame |

### Endgame

| # | Issue (plan ID) | Weeks | Blocked by | Steps | Done when |
|---|---|---|---|---|---|
| [ICS-067](https://github.com/MatthewK84/ICS/issues/67) | Build the IMM two-body tracker (CPP-26) | 10 | 046, 061 | 1) Port ICS-046. 2) Seed tracks from PLI. 3) Run parity tests. | Matches golden files |
| [ICS-068](https://github.com/MatthewK84/ICS/issues/68) | Solve closest approach and contact time (CPP-26) | 5 | 067 | 1) Port the solver. 2) Use same-frame relative measurements for miss distance. | Meets C7, C8 and C16 |

### Association

| # | Issue (plan ID) | Weeks | Blocked by | Steps | Done when |
|---|---|---|---|---|---|
| [ICS-069](https://github.com/MatthewK84/ICS/issues/69) | Build 2D tracklets, path-b (CPP-27) | 12 | 064 | 1) Gate detections frame to frame. 2) Assign with the Hungarian algorithm. 3) Handle splits and merges. | Parity with ICS-049 |
| [ICS-070](https://github.com/MatthewK84/ICS/issues/70) | Match tracklets across stations, path-b (CPP-27) | 12 | 061, 069 | 1) Gate by line-of-sight distance. 2) Score candidate pairs. | ≥95% correct matches on synthetic data |
| [ICS-071](https://github.com/MatthewK84/ICS/issues/71) | Manage 3D track hypotheses, path-b (CPP-27) | 20 | 070 | 1) Build track-oriented multiple-hypothesis tracking with a bounded hypothesis count. 2) Prune and merge. 3) Emit 3D tracks. | Meets C9 on V3 data |
| [ICS-072](https://github.com/MatthewK84/ICS/issues/72) | Score and prune tracks, path-b (CPP-27) | 6 | 071 | 1) Compute median residual per track. 2) Apply the 10 cm gate. 3) Tag track quality. | Quality flags match ICS-049 |

### Estimation and products

| # | Issue (plan ID) | Weeks | Blocked by | Steps | Done when |
|---|---|---|---|---|---|
| [ICS-073](https://github.com/MatthewK84/ICS/issues/73) | Fit the 7-state model, path-b (CPP-28) | 8 | 044, 072 | 1) Port the batch fit to Ceres with covariance. 2) Run parity tests. | Parity within 1e-6 relative |
| [ICS-074](https://github.com/MatthewK84/ICS/issues/74) | Fit lift and flutter models, path-b (CPP-28) | 8 | 045, 073 | 1) Port the models and model selection. 2) Run parity tests. | Parity with ICS-045 |
| [ICS-075](https://github.com/MatthewK84/ICS/issues/75) | Estimate fragment mass, path-b (CPP-28) | 5 | 050, 073 | 1) Combine β, projected area and the Cd table. 2) Propagate uncertainty. | Meets C10 |
| [ICS-076](https://github.com/MatthewK84/ICS/issues/76) | Build the kill classifier (CPP-29) | 6 | 052, 068, 073 | 1) Run the ballistic-fit test on the target body. 2) Count breakup pieces. 3) Check mission denial. | Meets C12 |
| [ICS-077](https://github.com/MatthewK84/ICS/issues/77) | Port the footprint Monte Carlo (CPP-30) | 6 | 051 | 1) Port ICS-051. 2) Run parity tests. | Parity; meets C11 |
| [ICS-078](https://github.com/MatthewK84/ICS/issues/78) | Deliver ics-pipeline (CPP-31) | 8 | 058 to 077 | 1) Orchestrate stages with checkpoints. 2) Retry with bounded backoff. | One run processes in ≤10 minutes |
| [ICS-079](https://github.com/MatthewK84/ICS/issues/79) | Build run records and provenance (CPP-32) | 8 | 013, 078 | 1) Build run records. 2) Write SHA-256 manifests. 3) Emit in-toto attestations adapted from verdict. | Any byte change breaks verification |
| [ICS-080](https://github.com/MatthewK84/ICS/issues/80) | Build the DroneScore export (CPP-33) | 3 | 079 | 1) Map run records to the DroneScore import. 2) Retry with bounded backoff. | 50 sample runs import cleanly |
| [ICS-081](https://github.com/MatthewK84/ICS/issues/81) | Deliver ics-api (CPP-34) | 6 | 012, 079 | 1) Serve read-only gRPC queries. 2) Configure Envoy gRPC-Web. | UI contract tests pass |

## M5 Front end

Build the read-only operator and evaluator interface on ics-api. All issues are `lang:ts` and `area:ui`; the range network allows no external calls, so serve every asset and map tile locally.

| # | Issue (plan ID) | Weeks | Blocked by | Steps | Done when |
|---|---|---|---|---|---|
| [ICS-082](https://github.com/MatthewK84/ICS/issues/82) | Scaffold the web app (UI-01) | 4 | 007 | 1) Create routes, layout and design tokens. 2) Put CAC/PKI authentication at the reverse proxy. 3) Add axe accessibility checks. | Lint, type and accessibility checks pass |
| [ICS-083](https://github.com/MatthewK84/ICS/issues/83) | Generate the typed API client (UI-02) | 4 | 081, 082 | 1) Generate protobuf-es types with Connect gRPC-Web. 2) Validate responses with zod at the boundary. 3) Retry with bounded backoff. | Contract tests pass; no untyped data reaches components |
| [ICS-084](https://github.com/MatthewK84/ICS/issues/84) | Build the range status view (UI-03) | 6 | 083 | 1) Show stations, time quality, GNSS and holdover. 2) Show mount pointing, sun cone and trigger state. 3) Stream server-pushed updates. | ≤1 s display latency |
| [ICS-085](https://github.com/MatthewK84/ICS/issues/85) | Build the engagement timeline view (UI-04) | 4 | 083 | 1) Plot C4 time gates per run. 2) Compare runs side by side. | Matches ICS-055 output on sample runs |
| [ICS-086](https://github.com/MatthewK84/ICS/issues/86) | Build the run quick-look (UI-05) | 6 | 083 | 1) Show kill class, miss distance ±σ and intercept point. 2) Show coverage and reduced-truth flags. | Available within 15 minutes of each run |
| [ICS-087](https://github.com/MatthewK84/ICS/issues/87) | Build the 3D engagement viewer (UI-06) | 8 | 083 | 1) Render stations, trajectories and debris in three.js. 2) Add time scrubbing. | 5,000 tracks render at ≥30 fps |
| [ICS-088](https://github.com/MatthewK84/ICS/issues/88) | Build the footprint map (UI-07) | 5 | 083 | 1) Render offline tiles in MapLibre. 2) Draw 50, 90 and 99% polygons and keep-out areas. | Zero external network calls |
| [ICS-089](https://github.com/MatthewK84/ICS/issues/89) | Build imagery review (UI-08) | 8 | 083 | 1) Scrub server-transcoded clips frame by frame. 2) Overlay tracks and detections. | Overlays align to the frame |
| [ICS-090](https://github.com/MatthewK84/ICS/issues/90) | Build the evaluator workflow (UI-09) | 8 | 086 | 1) Capture Section 5.2 sign-off. 2) Adjudicate no-tests. 3) Override kill class with a required reason. 4) Keep a full audit trail. | Every override stores user, time and reason |
| [ICS-091](https://github.com/MatthewK84/ICS/issues/91) | Build the validation dashboard (UI-10) | 4 | 056, 083 | 1) Show C1 to C19 status from ICS-056. 2) Link each result to its evidence. | Pass or fail shown per criterion with evidence links |

## M6 Integration, validation and release

Prove the system on the bench, then in the field in validation-event order, then release with the full evidence package. Label these issues `area:validation`.

| # | Issue (plan ID) | Weeks | Blocked by | Steps | Done when |
|---|---|---|---|---|---|
| [ICS-092](https://github.com/MatthewK84/ICS/issues/92) | Stand up the HIL bench (INT-02) | 8 | 018, 047, 078 | 1) Replay PLI and synthetic imagery through every service. 2) Run the bench nightly in CI. | Nightly CI produces a full run record |
| [ICS-093](https://github.com/MatthewK84/ICS/issues/93) | Shake down one station in the field: V0 and V1 (INT-03) | 10 | 029, 041 | 1) Emplace and survey one station. 2) Run star calibration and PPS injection. 3) Run strobe timing checks. 4) Test the sun interlock. | C1, C2, C17 and C18 pass |
| [ICS-094](https://github.com/MatthewK84/ICS/issues/94) | Fly the three-station envelope campaign: V2 (INT-04) | 16 | 093 | 1) Emplace all stations per Section 4.7. 2) Fly the sky-fiducial drone and dumbbell across the envelope, day and night. 3) Score with ICS-056. | C3 to C6, C14 to C16 and C19 pass |
| [ICS-095](https://github.com/MatthewK84/ICS/issues/95) | Run debris drops and suspended-target strikes: V3 and V4 (INT-05) | 12 | 094 | 1) Drop tagged components from 100, 300 and 1,000 m. 2) Fit the drag table with ICS-050 on half the drops. 3) Run suspended-target strikes. | C7 to C12 pass |
| [ICS-096](https://github.com/MatthewK84/ICS/issues/96) | Run live-run concurrence and assemble accreditation: V5 (INT-06) | 16 | 095, 056 | 1) Score the first live runs with all stations and truth pods. 2) Assemble the Section 6.4 package. | Evaluator and M&S Analyst sign |
| [ICS-097](https://github.com/MatthewK84/ICS/issues/97) | Release with the DoWI 8430.01 evidence package (INT-07) | 8 | 096 | 1) Publish signed SBOM and provenance. 2) Complete penetration testing and secure-code review. 3) Submit to the Authorizing Official. | AO accepts the evidence package |

## Effort totals

Direct task effort totals 625 person-weeks across 97 issues. The 14 `path-b` issues account for 131 person-weeks.

| Milestone | Issues | Person-weeks | Main languages |
|---|---|---|---|
| M0 Foundation and contracts | 14 | 31 | All |
| M1 Core services | 17 | 103 | C++ |
| M2 Station control | 10 | 73 | C++, firmware, Python |
| M3 Python models | 16 | 101 | Python |
| M4 Algorithms | 24 | 190 | C++ |
| M5 Front end | 10 | 57 | TypeScript |
| M6 Integration and release | 6 | 70 | All |
| Total | 97 | 625 |  |
