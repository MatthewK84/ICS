"""The reference IMM two-body tracker and closest-approach solver (ICS-046, PY-04).

The C++ endgame (CPP-26: ICS-067 and ICS-068) must match it through golden/imm:

- motion: the constant-velocity, constant-acceleration and coordinated-turn
  models, all in the cue's nine-number ENU state;
- imm: the interacting multiple model filter for one body, fed timed 3D
  positions such as multi-station triangulation gives;
- approach: the closest approach of two estimates at one time, its miss
  distance and first-order sigma, and the time of contact;
- truth: exact synthetic flights for the tests and the golden files.
"""
