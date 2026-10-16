"""The reference pointing model for a station mount (ICS-035, PY-01).

A TPOINT-style model maps the mount's encoder angles to where its line of
sight points, fitted to stars observed on a calibration night:

- model: the terms, and the model applied both ways;
- astrometry: a star's observed place, through ERFA;
- observations: a night's observations, as the capture side writes them;
- fit: least squares, outlier rejection and the held-out check (C17);
- plan: which stars to take on a night;
- parameters: the versioned parameter file ICS-036's encoder module loads;
- synth: synthetic nights, to prove the fit.

Run `python -m ics_models.pointing --help` for the command line.
"""
