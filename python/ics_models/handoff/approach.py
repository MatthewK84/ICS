"""When an interceptor is closest to its target (ICS-039).

Both vehicles are taken to keep the velocity they have now, so their
separation is closest where it stops closing: after (p · v) / (v · v) seconds,
for the interceptor's position p from the target and the closing velocity v,
the target's less the interceptor's. This is coarse, and it is for putting
engagements in order: the trigger (ICS-040) predicts the moment itself.

There is no approach to predict while the interceptor is slower than the
launch speed, for it has not launched; while the two are not closing; or
further ahead than the horizon, where the velocities will not last.
"""

import math
from dataclasses import dataclass

from ics_models.cue.kinematics import AXES, NANOSECONDS_PER_SECOND, Estimate


class HandoffError(ValueError):
    """An input the hand-off scheduler cannot use."""


@dataclass(frozen=True)
class ApproachSettings:
    """When there is an approach to predict."""

    launch_speed_m_s: float
    horizon_ns: int

    def __post_init__(self) -> None:
        """Refuse a launch speed that is not positive, and a horizon that is not."""
        if not (math.isfinite(self.launch_speed_m_s) and self.launch_speed_m_s > 0.0 and self.horizon_ns > 0):
            message = f"a launch speed of {self.launch_speed_m_s} m/s and a {self.horizon_ns} ns horizon cannot be used"
            raise HandoffError(message)


@dataclass(frozen=True)
class Approach:
    """A closest approach: its UTC time in nanoseconds, and the miss distance in metres."""

    time_ns: int
    miss_m: float


def closest_approach(target: Estimate, interceptor: Estimate, settings: ApproachSettings) -> Approach | None:
    """The closest approach ahead of two estimates at one time, or None when there is none to predict."""
    if target.time_ns != interceptor.time_ns:
        message = f"the estimates are at {target.time_ns} ns and {interceptor.time_ns} ns, not one time"
        raise HandoffError(message)
    position = [float(interceptor.state[axis] - target.state[axis]) for axis in range(AXES)]
    closing = [float(target.state[AXES + axis] - interceptor.state[AXES + axis]) for axis in range(AXES)]
    speeds = [float(interceptor.state[AXES + axis]) for axis in range(AXES)]
    speed_squared = sum(rate * rate for rate in speeds)
    closing_squared = sum(rate * rate for rate in closing)
    gained = sum(position[axis] * closing[axis] for axis in range(AXES))
    launched = speed_squared >= settings.launch_speed_m_s * settings.launch_speed_m_s
    if not (launched and gained > 0.0):
        return None
    seconds = gained / closing_squared
    if seconds * NANOSECONDS_PER_SECOND >= float(settings.horizon_ns):
        return None
    apart = [position[axis] - closing[axis] * seconds for axis in range(AXES)]
    miss = math.sqrt(sum(offset * offset for offset in apart))
    return Approach(target.time_ns + math.floor(seconds * NANOSECONDS_PER_SECOND + 0.5), miss)
