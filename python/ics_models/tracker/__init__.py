"""The reference closed-loop tracker's arithmetic (ICS-038, CPP-19).

The C++ port, ics::tracker in cpp/tracker, must match it through golden/tracker:

- centroid: the target in a window of a frame, against the window's own sky
  background, as a sub-pixel centroid;
- geometry: a pixel's direction on the sky, through the camera's boresight,
  plate scale, roll and mirroring and the mount's line of sight;
- estimator: the target's sky direction and rate, as an alpha-beta track;
- demand: the axis rates that follow the target and close the pointing error.
"""
