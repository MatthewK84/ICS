"""When an interceptor is closest to its target, to fire the cameras by (ICS-040).

The hand-off scheduler's approach (ICS-039) takes both vehicles to keep their
velocity. This one starts there and takes them to keep their acceleration
too: the separation s(t) = p - c t - g t² / 2, for the interceptor's position p
from the target, the closing velocity c and the closing acceleration g, each
the target's less the interceptor's. The separation is closest where
s(t) · s'(t) = 0, found by a fixed number of Newton steps from the
constant-velocity answer.

The steps are kept only if every one of them had the separation curving up,
and they end ahead of now and inside the horizon. Otherwise the
constant-velocity answer stands.
"""

import math

from ics_models.cue.kinematics import AXES, NANOSECONDS_PER_SECOND, Estimate
from ics_models.handoff.approach import Approach, ApproachSettings, closest_approach

NEWTON_STEPS: int = 8


def closing_of(target: Estimate, interceptor: Estimate) -> tuple[list[float], list[float], list[float]]:
    """The interceptor's position from the target, and the closing velocity and acceleration."""
    position = [float(interceptor.state[axis] - target.state[axis]) for axis in range(AXES)]
    velocity = [float(target.state[AXES + axis] - interceptor.state[AXES + axis]) for axis in range(AXES)]
    acceleration = [float(target.state[2 * AXES + axis] - interceptor.state[2 * AXES + axis]) for axis in range(AXES)]
    return position, velocity, acceleration


def dot(first: list[float], second: list[float]) -> float:
    """first · second."""
    return sum(first[axis] * second[axis] for axis in range(AXES))


def separation_after(closing: tuple[list[float], list[float], list[float]], seconds: float) -> list[float]:
    """The interceptor's position from the target after some seconds."""
    position, velocity, acceleration = closing
    half = seconds * seconds / 2.0
    return [position[axis] - velocity[axis] * seconds - acceleration[axis] * half for axis in range(AXES)]


def refined_seconds(closing: tuple[list[float], list[float], list[float]], start: float) -> float | None:
    """The seconds to the closest approach, by Newton steps from a start; None if a step had no minimum to find."""
    _, velocity, acceleration = closing
    seconds = start
    usable = True
    for _ in range(NEWTON_STEPS):
        apart = separation_after(closing, seconds)
        rate = [-velocity[axis] - acceleration[axis] * seconds for axis in range(AXES)]
        slope = dot(rate, rate) - dot(apart, acceleration)
        if usable and slope > 0.0:
            seconds = seconds - dot(apart, rate) / slope
        else:
            usable = False
    return seconds if usable else None


def refined_approach(target: Estimate, interceptor: Estimate, settings: ApproachSettings) -> Approach | None:
    """The closest approach ahead of two estimates at one time, or None when there is none to predict."""
    coarse = closest_approach(target, interceptor, settings)
    if coarse is None:
        return None
    closing = closing_of(target, interceptor)
    start = (coarse.time_ns - target.time_ns) / NANOSECONDS_PER_SECOND
    seconds = refined_seconds(closing, start)
    # Ahead of now and inside the horizon: within half the horizon of its middle.
    middle = float(settings.horizon_ns) / 2.0
    if seconds is None or not abs(seconds * NANOSECONDS_PER_SECOND - middle) < middle:
        return coarse
    apart = separation_after(closing, seconds)
    time_ns = target.time_ns + math.floor(seconds * NANOSECONDS_PER_SECOND + 0.5)
    return Approach(time_ns, math.sqrt(dot(apart, apart)))
