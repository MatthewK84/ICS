"""The reference trigger controller's arithmetic (ICS-040, CPP-21).

The C++ port, ics::trigger in cpp/trigger, must match it through golden/trigger:

- approach: when an interceptor is closest to its target, with both taken to
  keep the acceleration they have now;
- shot: the PPS edge a pulse is timed from, the delay the pulse generator is
  given from that edge, and when the generator may be armed for it.
"""
