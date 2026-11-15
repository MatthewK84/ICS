"""The reference hand-off scheduler's arithmetic (ICS-039, CPP-20).

The C++ port, ics::handoff in cpp/handoff, must match it through golden/handoff:

- approach: when an interceptor is closest to its target, from their two
  estimates;
- slew: the axis rates that take a mount to a new target as fast as its rate
  and deceleration allow, and how long that takes;
- queue: the engagements in the order they will happen, the one the station
  serves, and the flags on those it cannot reach.
"""
