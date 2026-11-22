"""The reference camera model and its calibration (ICS-042, PY-02).

Each station camera, narrow, wide and MWIR, is a pinhole with Brown-Conrady
distortion, fitted to views of a planar target, and a boresight: the
camera's small rotation off the mount's line of sight, fitted to sightings of
known directions on the sky.

- model: the intrinsics, distortion, projection and undistortion;
- boresight: a direction's pixel and a pixel's direction, from the line of sight;
- pose: target-board poses, homographies and the closed-form seed;
- solve: the Levenberg-Marquardt solver both fits use;
- fit: the intrinsics and boresight fits, and the held-out check (C3);
- observations: the target-point and sighting files, as the capture side writes them;
- parameters: the versioned parameter file ICS-058's camera model loads;
- synth: synthetic calibrations, to prove the fits.

Run `python -m ics_models.camera --help` for the command line.
"""
