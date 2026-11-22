# golden

Cross-language golden test vectors. Python references and reference tools such as GeographicLib publish them; C++ ports must match them in parity tests before they merge.

**Language:** data files shared by C++, Python and TypeScript. **Lead role:** Modeling engineer.

Contents:

- [`proto/`](proto): one instance of each ICS protobuf message with every field set, written by [`python/ics_golden/proto.py`](../python/ics_golden/proto.py). The C++ and TypeScript tests must read each file and write back the same bytes ([ICS-012](https://github.com/MatthewK84/ICS/issues/12); see [`proto/README.md`](../proto/README.md#golden-files)).
- [`pli/`](pli): one segment of the C++ PLI store and its two Parquet files, written by the store's own Parquet writer ([ICS-030](https://github.com/MatthewK84/ICS/issues/30), [`cpp/store`](../cpp/README.md#pli-store)). The C++ test `ArchiveSegment.MatchesTheGoldenFiles` requires the store to write the same bytes; run it with `ICS_WRITE_GOLDEN=1` to write them again after a deliberate change. [`python/tests/test_pli_parquet.py`](../python/tests/test_pli_parquet.py) reads the segment with its own parser and requires pyarrow, an independent Parquet reader, to find the same columns, types, nulls and values in the Parquet files.
- [`frames/`](frames): geodetic to ECEF, geodetic to range ENU, and EGM96 MSL to ellipsoid height, generated with GeographicLib 2.3 by [`frames/generate.sh`](frames/generate.sh) from the case lists in [`frames/inputs/`](frames/inputs). The C++ frames module, [`cpp/frames`](../cpp/frames), matches every vector within 1 mm in [`cpp/frames/test/golden_test.cpp`](../cpp/frames/test/golden_test.cpp) ([ICS-014](https://github.com/MatthewK84/ICS/issues/14), [ICS-017](https://github.com/MatthewK84/ICS/issues/17)); see [`docs/frames-and-time.md`](../docs/frames-and-time.md#golden-vectors).

- [`pointing/`](pointing): the pointing model ([ICS-035](https://github.com/MatthewK84/ICS/issues/35)), written by [`python/ics_golden/pointing.py`](../python/ics_golden/pointing.py).
  - `model.json` is a parameter file whose made-up model uses every term.
  - `vectors.csv` gives, across azimuth and from 5° to 85° of elevation, the encoder angles and the sky direction the model gives for them.

  The C++ encoder module ([ICS-036](https://github.com/MatthewK84/ICS/issues/36)) loads the model, then gives each sky direction from its encoder angles, and each encoder angle pair from its sky direction, within 1e-12 rad ([`cpp/encoder/test/pointing_model_test.cpp`](../cpp/encoder/test/pointing_model_test.cpp)). [`python/tests/test_pointing_io.py`](../python/tests/test_pointing_io.py) requires the files to be what the reference writes.

- [`camera/`](camera): the camera model ([ICS-042](https://github.com/MatthewK84/ICS/issues/42)), written by [`python/ics_golden/camera.py`](../python/ics_golden/camera.py).
  - `narrow.json`, `wide.json` and `mwir.json` are parameter files for three made-up cameras. The wide camera mirrors and has all five distortion terms.
  - `project.csv` gives points in the camera frame, at three depths across a grid of pixels from corner to corner, and their pixels.
  - `undistort.csv` gives each grid pixel's normalised image point.
  - `sky.csv` gives each grid pixel's direction on the sky along four lines of sight.

  The C++ camera model ([ICS-058](https://github.com/MatthewK84/ICS/issues/58)) must give each pixel from its point, each normalised point from its pixel, each direction from its pixel and each pixel from its direction, within 1e-12 of the larger of the number and 1. [`python/tests/test_camera_io.py`](../python/tests/test_camera_io.py) requires the files to be what the reference writes.

- [`cue/`](cue): the cue extrapolator ([ICS-037](https://github.com/MatthewK84/ICS/issues/37)), written by [`python/ics_golden/cue.py`](../python/ics_golden/cue.py).
  - `parameters.csv` is the filter's settings.
  - `reports.csv` is eight seconds of a manoeuvring target's PLI and radar in arrival order. Each row gives the report, the filter's outcome and its estimate after the report.
  - `queries.csv` gives estimates after the last report, up to and past the coast limit.
  - `cues.csv` gives three stations' cues, and a target overhead that has none.
  - `measurements.csv` gives PLI points and velocities in the range frame. The first point is the range origin.

  The C++ cue module matches every outcome, every state and covariance value within 1e-9, and every angle within 1e-10 rad ([`cpp/cue/test/golden_test.cpp`](../cpp/cue/test/golden_test.cpp)). [`python/tests/test_cue_golden.py`](../python/tests/test_cue_golden.py) requires the files to be what the reference writes, every number within 1e-12 of the larger of it and 1. OpenBLAS picks its matrix kernels by CPU, so a number's last bit can differ between machines.

- [`tracker/`](tracker): the closed-loop tracker ([ICS-038](https://github.com/MatthewK84/ICS/issues/38)), written by [`python/ics_golden/tracker.py`](../python/ics_golden/tracker.py).
  - `frame-00.pgm` to `frame-06.pgm` are 64 × 48 16-bit frames, as plain PGM: a noisy sky with dark and bright targets, by day and by night.
  - `centroids.csv` gives the target found in windows of those frames, or none.
  - `windows.csv` gives search windows for centres near and past the frame's edges.
  - `geometry.csv` gives pixels as tangent-plane offsets and sky directions, and back, for cameras rolled and mirrored.
  - `estimator.csv` is an alpha-beta track of a weaving target, measured every 10 ms.
  - `demands.csv` gives rate demands for targets and mounts at random states.

  The C++ tracker matches every outcome and window exactly, and every number within 1e-9 of the larger of it and 1 ([`cpp/tracker/test/golden_test.cpp`](../cpp/tracker/test/golden_test.cpp)). [`python/tests/test_tracker_golden.py`](../python/tests/test_tracker_golden.py) requires the files to be what the reference writes, every number within 1e-12 of the larger of it and 1.

- [`handoff/`](handoff): the hand-off scheduler ([ICS-039](https://github.com/MatthewK84/ICS/issues/39)), written by [`python/ics_golden/handoff.py`](../python/ics_golden/handoff.py).
  - `parameters.csv` is the settings the other files were written with.
  - `approaches.csv` gives the closest approach of a target and an interceptor at random states, or none: parked, receding, or past the horizon.
  - `slews.csv` gives slew demands for targets and mounts at random states, with small, middling and large errors.
  - `reaches.csv` gives the time a mount takes between two directions.
  - `queue.csv` is a scripted run of four engagements, with a row for each engagement at each update: the sighting given, and the slot, the order and the engagement served after it.

  The C++ scheduler matches every flag, place and engagement served exactly, every approach's time within 2 ns, and every other number within 1e-9 of the larger of it and 1 ([`cpp/handoff/test/golden_test.cpp`](../cpp/handoff/test/golden_test.cpp)). [`python/tests/test_handoff_golden.py`](../python/tests/test_handoff_golden.py) requires the files to be what the reference writes, every number within 1e-12 of the larger of it and 1.

- [`trigger/`](trigger): the trigger controller ([ICS-040](https://github.com/MatthewK84/ICS/issues/40)), written by [`python/ics_golden/trigger.py`](../python/ics_golden/trigger.py).
  - `parameters.csv` is the settings the other files were written with.
  - `approaches.csv` gives the refined closest approach of a target and an interceptor at random states with accelerations, gentle and violent, or none: parked or receding.
  - `shots.csv` gives the PPS edge, the delay and the late flag of pulses asked for at random times around now, and whether the arming window is open.

  The C++ controller matches every flag, edge and delay exactly, every approach's time within 2 ns, and every miss within 1e-9 of the larger of it and 1 ([`cpp/trigger/test/golden_test.cpp`](../cpp/trigger/test/golden_test.cpp)). [`python/tests/test_trigger_golden.py`](../python/tests/test_trigger_golden.py) requires the files to be what the reference writes, every number within 1e-12 of the larger of it and 1.

- [`ballistic/`](ballistic): the 7-state ballistic model ([ICS-044](https://github.com/MatthewK84/ICS/issues/44)), written by [`python/ics_golden/ballistic.py`](../python/ics_golden/ballistic.py).
  - `parameters.csv` is the settings the other files were written with: the range origin's height, gravity, the RK4 step, and the fit's settings.
  - `atmosphere.csv` gives ISA density from -500 m to 11 km.
  - `trajectories.csv` gives six pieces, from no drag to foil-like drag, propagated every 73 ms for about 3 s, so most times fall between RK4 steps.
  - `measurements.csv` gives those pieces' positions every 10 ms for 2 s, with 2 cm of noise on each axis.
  - `fits.csv` gives each piece's true state, the fitted state, the fit's iterations, convergence and RMS residual, and the covariance's upper triangle, row by row.

  The C++ estimation port (CPP-28, [ICS-073](https://github.com/MatthewK84/ICS/issues/73)) must match the densities and trajectories within 1e-9 of the larger of the value and 1, and the fitted states within 1e-6 relative. [`python/tests/test_ballistic_golden.py`](../python/tests/test_ballistic_golden.py) requires the files to be what the reference writes, every number within 1e-12 of the larger of it and 1.

- [`imm/`](imm): the IMM two-body tracker and closest approach ([ICS-046](https://github.com/MatthewK84/ICS/issues/46)), written by [`python/ics_golden/imm.py`](../python/ics_golden/imm.py).
  - `parameters.csv` is the settings the other files were written with: the four models (constant velocity, constant acceleration, and turns at ±0.5 rad/s), the switching matrix, the start, and the horizon and contact radius.
  - `motion.csv` gives each model's transition and process noise for steps of 1 ms, 10 ms and 730 ms, row by row.
  - `measurements.csv` is a scripted engagement measured every 10 ms with 5 mm of noise on each axis: the target flies straight, turns left, then accelerates; the interceptor accelerates and passes 0.3 m from it 5 s after the start.
  - `tracks.csv` gives each body's model chances and combined state and covariance (upper triangle, row by row) after every tenth measurement and the last.
  - `encounters.csv` gives the encounter of the two tracks at the same times, or none.
  - `approaches.csv` gives encounters for random estimate pairs with diagonal covariances, and for the edge cases: receding, parked, past the horizon, in contact already, a direct hit, a pass while accelerating, one that falls short, and one that recedes then comes back.

  The C++ endgame (CPP-26, [ICS-067](https://github.com/MatthewK84/ICS/issues/67) and [ICS-068](https://github.com/MatthewK84/ICS/issues/68)) must match every flag exactly, every time within 2 ns, and every other number within 1e-9 of the larger of it and 1. [`python/tests/test_endgame_golden.py`](../python/tests/test_endgame_golden.py) requires the files to be what the reference writes, every number within 1e-12 of the larger of it and 1.
- [`aero/`](aero): the lift and flutter models and model selection ([ICS-045](https://github.com/MatthewK84/ICS/issues/45)), written by [`python/ics_golden/aero.py`](../python/ics_golden/aero.py). States are written in named columns, position, velocity, `kappa`, `kappa_slope` (flutter), `lift_kappa` and `side_kappa` (lift), with 0 for a parameter the model lacks.
  - `parameters.csv` is the settings the other files were written with, the same as `ballistic/`'s.
  - `truths.csv` gives six pieces, two following each model (ballistic, flutter, lift), and the model each follows.
  - `trajectories.csv` gives those pieces propagated every 73 ms for about 3 s under their own model.
  - `measurements.csv` gives their positions every 10 ms for 2 s, with 2 cm of noise on each axis.
  - `fits.csv` gives every candidate model's fit to every piece: iterations, convergence, RMS residual, BIC, the fitted state and its standard deviations.
  - `covariances.csv` gives each fit's covariance's upper triangle, by row and column in the model's own state order.
  - `selection.csv` gives the model chosen for each piece; it is always the one the piece follows.

  The C++ port (CPP-28, [ICS-074](https://github.com/MatthewK84/ICS/issues/74)) must match the trajectories within 1e-9 of the larger of the value and 1, the fitted states within 1e-6 relative, and every choice exactly. [`python/tests/test_aero_golden.py`](../python/tests/test_aero_golden.py) requires the files to be what the reference writes, every number within 1e-12 of the larger of it and 1, and every true-model fit to find its parameters within 3σ.

Planned contents:

- Golden outputs from the other Python reference models in `python/ics_models` (for example [ICS-046](https://github.com/MatthewK84/ICS/issues/46) to [ICS-049](https://github.com/MatthewK84/ICS/issues/49)).

Rules: golden files change only through the reference that produces them, and every change is reviewed with that reference. The Python references write and check them with OpenBLAS's Haswell kernels (`OPENBLAS_CORETYPE=Haswell`, which `ics_golden` and the tests' `conftest.py` set unless it is already set, and the Python workflow pins): other kernels round matrix products differently, and an iterative fit can then take another path.

Next issue: [ICS-046](https://github.com/MatthewK84/ICS/issues/46), the IMM tracker and closest approach.
