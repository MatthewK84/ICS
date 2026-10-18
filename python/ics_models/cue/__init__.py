"""The reference cue extrapolator for one target (ICS-037, CPP-18).

The C++ port, ics::cue in cpp/cue, must match it through golden/cue:

- kinematics: the constant-acceleration state, and its prediction with
  white-jerk process noise;
- geometry: WGS 84 positions in the range ENU frame, and the rotation into a
  station's own ENU axes;
- filter: the Kalman filter that fuses position reports, gates outliers and
  re-filters a late report from the checkpoint before it;
- pointing: a station's azimuth, elevation, their rates and the one-sigma
  pointing uncertainty, from an estimate.
"""
