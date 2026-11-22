# python

Reference models, calibration fitting, synthetic data, statistics, validation scoring and ML training. Python never runs in the live capture or control path.

**Language:** Python 3.12. **Lead role:** Modeling engineer.

Planned layout:

```text
python/
  ics_golden/   golden test vectors shared with C++ and TypeScript (golden/), and the run-record samples (schemas/)
  ics_models/   pointing (ICS-035), cue (ICS-037), tracker (ICS-038), handoff (ICS-039), trigger (ICS-040), ballistic (ICS-044, ICS-045), and to
                come: calibration, trajectory, drag, footprint, killclass, uncertainty
  ics_synth/    synthetic engagement generator
  ics_stats/    Pk, timeline analysis, validation scorer
  ics_sitl/     the SITL rig's ground station (ICS-018, deploy/sitl): MAVLink, missions, engagement checks,
                and cut-log, which cuts onboard logs into test fixtures (ICS-025)
```

`ics_sitl` is test equipment, not a model: it flies the SITL rig's scripted engagements ([`deploy/sitl`](../deploy/sitl/README.md)). It uses only the standard library, so the rig runs on any Python 3.12 without `uv`.

Rules (enforced in CI by [ICS-006](https://github.com/MatthewK84/ICS/issues/6)):

- Full type annotations, `mypy --strict`, `ruff`; no `noqa` or `type: ignore`.
- No recursion, functions of 50 lines or fewer, no module-level mutable state.
- Every algorithm is written here first as a reviewed reference with golden outputs in `golden/`; the C++ port must match it.
- Parameter files are versioned and schema-validated; C++ loads them at startup.

## Toolchain

Python 3.12 comes from the operating system (Ubuntu 24.04 ships 3.12.3), and every dependency is pinned with hashes in [`uv.lock`](uv.lock). uv itself is pinned in [`requirements-uv.txt`](requirements-uv.txt), and `pyproject.toml` refuses any other uv version. All tool settings live in [`pyproject.toml`](pyproject.toml).

```sh
cd python
python3.12 -m venv /tmp/uv && /tmp/uv/bin/pip install --require-hashes --no-deps -r requirements-uv.txt
export PATH="/tmp/uv/bin:${PATH}"
uv sync --locked                                     # install the locked dependencies into .venv
uv run --locked ruff check . && uv run --locked ruff format --check .
uv run --locked mypy .                               # strict mode
uv run --locked python -m ics_lint . --exclude policy/seeded --exclude gen
uv run --locked pytest                               # hypothesis, coverage of at least 90%
policy/check-policy.sh
```

`gen/` holds the protobuf messages generated from [`proto/`](../proto/README.md), imported as `ics.<package>`, for example `from ics.v1 import run_record_pb2`. They are generated and committed, never edited, so ruff and the AST checks skip them, and mypy reads their stubs without reporting on them.

To add or upgrade a dependency, change `pyproject.toml`, run `uv lock`, and record the dependency in the [dependency register](../docs/dependency-register.md).

## Pointing model

[`ics_models/pointing`](ics_models/pointing) is the reference pointing model for a station mount ([ICS-035](https://github.com/MatthewK84/ICS/issues/35), PY-01). It maps the mount's encoder angles to where its line of sight truly points, with TPOINT-style terms fitted to stars observed on a calibration night. Its angles and time follow [docs/frames-and-time.md](../docs/frames-and-time.md#mount-pointing).

| Module | Provides |
|---|---|
| `model` | The terms (the basic alt-az set IA, IE, CA, NPAE, AN, AW and TF, and harmonics an operator may add), and the model both ways: `to_sky` and `to_encoder` |
| `astrometry` | A star's observed place, through ERFA's `atco13` (pyerfa) |
| `observations` | The night's observation file, as the capture side writes it, and its reduction to sightings |
| `fit` | Linear least squares, rejecting stars more than 3 times the RMS off (at most 5 passes); the held-out RMS (C17); the split |
| `plan` | Which catalog stars to take on a night, spread across azimuth and 20° to 85° of elevation |
| `parameters` | The versioned parameter file, checked against [`schemas/pointing-model.schema.json`](../schemas/pointing-model.schema.json) |
| `synth` | Synthetic nights, through a known model with centroid noise |

```sh
uv run --locked python -m ics_models.pointing plan catalog.csv --site 32.38,-106.48,1200 \
    --start 2026-10-16T03:00:00Z --end 2026-10-16T09:00:00Z --count 60 --dut1 0.12 --out plan.csv
uv run --locked python -m ics_models.pointing fit night.csv --station s1 --model-id s1-2026-10-16 --out model.json
```

- **Observations.** One CSV row per star on the boresight, with:
  - the UTC time at mid-exposure and the night's DUT1 (UT1 − UTC, from IERS Bulletin A);
  - the star's catalog place, as Hipparcos and Gaia give it;
  - the encoder angles, with the star's offset from the boresight already applied by the capture side;
  - the night's air and the camera's wavelength, and the site.

  A star below 20° elevation is refused, and so is a night without DUT1.
- **The fit** holds a quarter of the stars out at random, fits the rest, and writes the model with its RMS on the stars held out, which is C17. It reports each term's value and standard deviation; there is no automatic term selection, so an operator adds a harmonic only when the data need it.
- **Done when.** On 50 synthetic nights of 60 stars, with 3 µrad of centroid noise per axis, the worst held-out RMS is under C17's 10 µrad. Every term lies within 5σ of the truth, and at least 98% within 3σ. A model missing a term the data need fails C17. The star positions match SOFA's own test case for `atco13` to 1e-12 rad.
- **On the sky.** The real capture and C17 on stars are the field shakedown's ([ICS-093](https://github.com/MatthewK84/ICS/issues/93)).
- **Golden vectors** for the C++ port are in [`golden/pointing`](../golden/README.md).

## Cue extrapolator

[`ics_models/cue`](ics_models/cue) is the reference cue extrapolator for one target ([ICS-037](https://github.com/MatthewK84/ICS/issues/37), CPP-18). The C++ port, `ics::cue` ([`cpp/cue`](../cpp/README.md#cue-extrapolator)), must match it through [`golden/cue`](../golden/README.md).

| Module | Provides |
|---|---|
| `kinematics` | The constant-acceleration state in the range ENU frame, and `predict` with white-jerk process noise, exact over any step |
| `geometry` | WGS 84 points in the range ENU frame, and the turn from its axes into a station's |
| `filter` | `CueFilter`: `add`, with the chi-squared gate and the re-filtering of a late report from the checkpoint before it; `estimate_at`, within the history and the coast limit |
| `pointing` | `cue_for`: a station's azimuth, elevation, their rates and the one-sigma pointing uncertainty |

- **Golden vectors.** `python -m ics_golden.cue ../golden/cue` writes them. They cover a manoeuvring target's eight seconds of GNSS PLI and radar in arrival order, with every outcome (taken, late, gated, too old) and the estimate after each report. They also cover queries up to and past the coast limit, three stations' cues, and PLI points in the range frame. [`tests/test_cue_golden.py`](tests/test_cue_golden.py) requires the files to be what the reference writes, every number within 1e-12 of the larger of it and 1. OpenBLAS picks its matrix kernels by CPU, so a number's last bit can differ between machines.
- **Tests** check prediction composing exactly, a constant-acceleration truth followed exactly, late reports giving the in-order result, the gate, the history and its capacity, the coast limit, positions against `golden/frames`, angles, rates against differences, and the sigma.

## Tracker

[`ics_models/tracker`](ics_models/tracker) is the reference for the closed-loop tracker's arithmetic ([ICS-038](https://github.com/MatthewK84/ICS/issues/38), CPP-19). The C++ port, `ics::tracker` ([`cpp/tracker`](../cpp/README.md#tracker)), must match it through [`golden/tracker`](../golden/README.md).

| Module | Provides |
|---|---|
| `centroid` | `window_around`, a search window kept inside the frame; `find_target`: the background and its noise from the window's border (median, and MAD × 1.4826), a threshold of the larger of k·σ and a minimum contrast, 8-connected blobs, and the contrast-weighted centroid of the blob nearest the prediction |
| `geometry` | `CameraGeometry` (boresight, plate scale, roll, mirrored); pixels to tangent-plane offsets and back; tangent-plane offsets to sky directions about a line of sight, and back, by the gnomonic projection |
| `estimator` | `AngleEstimator`, an alpha-beta track of azimuth and elevation, with the azimuth residual wrapped |
| `demand` | `demand`: the target's rate plus gain times the pointing error, with the mount carried on at its rates to the same time |

- **Golden vectors.** `python -m ics_golden.tracker ../golden/tracker` writes them, from seed 38: seven 64 × 48 frames of a noisy sky with dark and bright targets, and the centroids, windows, geometry, estimator track and demands computed on them. [`tests/test_tracker_golden.py`](tests/test_tracker_golden.py) requires the files to be what the reference writes, every number within 1e-12 of the larger of it and 1.
- **Tests** check centroids against known spots, polarity, the area limits and the nearest blob; windows at and past every edge; pixel to sky and back for rolled and mirrored cameras and across north; the estimator following a constant-rate truth exactly and its stability bounds; and the demand's signs and wrap.

## Hand-off scheduler

[`ics_models/handoff`](ics_models/handoff) is the reference for the hand-off scheduler's arithmetic ([ICS-039](https://github.com/MatthewK84/ICS/issues/39), CPP-20). The C++ port, `ics::handoff` ([`cpp/handoff`](../cpp/README.md#hand-off-scheduler)), must match it through [`golden/handoff`](../golden/README.md).

| Module | Provides |
|---|---|
| `approach` | `closest_approach`: when an interceptor is closest to its target and by how much it misses, with both taken to keep their velocity; none for an interceptor not yet launched, vehicles not closing, or an approach past the horizon |
| `slew` | `slew_demand`: the cue's rate plus the rate that closes the error as fast as the mount can stop in, held within the axis's rate limit; `slew_seconds` and `reach_seconds`, how long a slew takes |
| `queue` | `Queue.update`: the engagements done, those not done in order, the one the station serves, and the `too_close` and `out_of_reach` flags |

- **Golden vectors.** `python -m ics_golden.handoff ../golden/handoff` writes them, from seed 39: approaches and slews at random states, slew times, and a scripted run of four engagements with a row for each engagement at each update. [`tests/test_handoff_golden.py`](tests/test_handoff_golden.py) requires the files to be what the reference writes, every number within 1e-12 of the larger of it and 1.
- **Tests** check approaches head-on, offset, parked, receding and past the horizon. They check the slew's two laws meeting with one slope, its rate limit, the short way across north, and a simulated mount arriving on time without overshoot. They check the queue's order, margin, dwell, fly-bys, approaches held and dropped, and both flags.

## Trigger controller

[`ics_models/trigger`](ics_models/trigger) is the reference for the trigger controller's arithmetic ([ICS-040](https://github.com/MatthewK84/ICS/issues/40), CPP-21). The C++ port, `ics::trigger` ([`cpp/trigger`](../cpp/README.md#trigger-controller)), must match it through [`golden/trigger`](../golden/README.md).

| Module | Provides |
|---|---|
| `approach` | `refined_approach`: when an interceptor is closest to its target and by how much it misses, with both taken to keep their acceleration. It takes eight Newton steps from the hand-off's `closest_approach`, and keeps that answer if the steps leave the time between now and the horizon |
| `shot` | `shot_for`: the PPS edge a pulse is timed from and its delay from that edge, or the next edge that can still be armed for when it is too late; `window_open`, when the pulse generator may be armed for it |

- **Golden vectors.** `python -m ics_golden.trigger ../golden/trigger` writes them, from seed 40: approaches at random states with gentle and violent accelerations, and shots for pulses asked for around now. [`tests/test_trigger_golden.py`](tests/test_trigger_golden.py) requires the files to be what the reference writes, every number within 1e-12 of the larger of it and 1.
- **Tests** check approaches for vehicles that keep their velocity, speed up and turn away, and for steps that find no minimum or leave the horizon. They check edges with an offset, pulses on time and too late for their edge, both ends of the arming window, and periods too short for their margins.

## Ballistic model

[`ics_models/ballistic`](ics_models/ballistic) is the reference 7-state ballistic model for debris ([ICS-044](https://github.com/MatthewK84/ICS/issues/44), PY-04). The C++ estimation port (CPP-28, [ICS-073](https://github.com/MatthewK84/ICS/issues/73)) must match it through [`golden/ballistic`](../golden/README.md). The state is position and velocity in the range ENU frame and κ = 1/β = Cd·A/m in m²/kg; ρ is not fitted but comes from the atmosphere at the piece's height.

| Module | Provides |
|---|---|
| `atmosphere` | `density`: ISA troposphere air density from -500 m to 11 km above mean sea level |
| `dynamics` | `Model` (range origin height, gravity, RK4 step); the point-mass drag acceleration a = -g·up - ½·ρ·κ·\|v\|·v, with flat-Earth gravity, no Coriolis and no wind; `propagate` and `trajectory`, fixed-step RK4 in whole-nanosecond steps that land exactly on each time asked for |
| `fit` | `fit`: the state at the first point and κ from timed 3D points with one σ per axis, by Levenberg-Marquardt with central-difference derivatives, seeded from a quadratic; the covariance (JᵀJ)⁻¹, the RMS 3D residual, the iterations and whether it converged; `fit_with` fits any `FitModel` (a law of motion with its derivative steps, floors and extra seeds) the same way |
| `aero` | ICS-045's models, as `FitModel`s: `LIFT`, 9 states, adds ½·ρ·(κ_L·(\|v\|²·up - v_up·v) + κ_S·\|v\|·(v × up)), lift and side force perpendicular to v that fade out as the path turns vertical; `FLUTTER`, 8 states, a tumbling piece's drag κ + κ₁·\|v\| |
| `select` | `select`: fits every candidate (ballistic, flutter, lift) to the same points and chooses the lowest BIC = χ² + k·ln(n), with k the state size and n the residual count; a fit that did not converge is never chosen |

- **Done when.** Noise-free points give back the true state and κ within 1e-6 relative for κ from 0 to 1 m²/kg. On 30 noisy trials with 2 cm per axis, the mean normalised squared error is within 5 to 9 (χ² with 7 degrees of freedom) and κ lies within 3σ in at least 28.
- **Golden vectors.** `python -m ics_golden.ballistic ../golden/ballistic` writes them, from seed 44: density by height, six pieces from no drag to foil-like drag propagated to times between RK4 steps and on them, and the same pieces measured with noise and fitted. [`tests/test_ballistic_golden.py`](tests/test_ballistic_golden.py) requires the files to be what the reference writes, every number within 1e-12 of the larger of it and 1.
- **Lift, flutter and selection (ICS-045).** On the golden pieces, every model fitted to its own pieces finds its parameters within 3σ, and selection chooses the true model for all six. `python -m ics_golden.aero ../golden/aero` writes them, from seed 45; [`tests/test_aero_golden.py`](tests/test_aero_golden.py) checks them as `test_ballistic_golden.py` does.
- **Out of scope.** Flutter as a periodic tumbling force (a frequency and phase, which need a seed search) is left for later; wind is the footprint model's (ICS-051).

## Power-of-Ten checks

The Python rules from the coding-standards table in [docs/build-plan.md](../docs/build-plan.md) are enforced by tools, not review (ICS-006):

| Rule | Enforced by |
|---|---|
| Full type annotations | `mypy --strict` |
| Early returns, no bare `except`, bandit's security checks, complexity and style | ruff, with the rule set in `pyproject.toml` |
| No recursion (ICS101); functions of 50 lines or fewer (ICS102); no module-level mutable state (ICS103) | [`ics_lint`](ics_lint), the AST checks |
| No suppressions | [`policy/check-policy.sh`](policy/check-policy.sh) rejects `noqa`, `type: ignore`, `pragma: no cover`, inline mypy or pyright settings, and any tool configuration outside `pyproject.toml` |
| Tests | pytest with hypothesis, deterministic examples, warnings as errors, and a coverage floor of 90% |

The policy script also proves each check still fires: every file in [`policy/seeded/`](policy/seeded) breaks exactly one rule and must be rejected with the message named on its first line.

`ics_lint` finds recursion through calls it can resolve inside one module: plain names, looked up as Python does, and `self.`/`cls.` method calls. Recursion across modules or through other objects is not detected. A module-level constant should be a tuple, a frozenset or another immutable type; lists, dicts, sets and `global` statements are rejected.

One exception is deliberate: tests may use plain `assert`, which pytest rewrites, so bandit's `S101` is off for `tests/` only.

Next issue: [ICS-007](https://github.com/MatthewK84/ICS/issues/7) (TypeScript toolchain). The first Python model is [ICS-035](https://github.com/MatthewK84/ICS/issues/35) (mount pointing model, milestone M3).
