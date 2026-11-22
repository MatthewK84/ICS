"""The reference 7-state ballistic model (ICS-044, PY-04).

A piece of debris is a point mass under gravity and drag. Its state is seven
numbers in the range ENU frame: position east, north and up (m), velocity
(m/s), and κ = 1/β = Cd·A/m (m²/kg), the inverse of its ballistic
coefficient. Air density comes from the ISA atmosphere at the piece's height,
so it is not fitted. The C++ port (CPP-28, ICS-073) must match it through
golden/ballistic:

- atmosphere: ISA troposphere density by height above mean sea level;
- dynamics: the acceleration and its fixed-step RK4 propagation;
- fit: the initial state and κ from timed 3D points, by Levenberg-Marquardt,
  with their covariance.
"""
