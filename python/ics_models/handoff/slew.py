"""The axis rates that take a mount to a new target, and how long that takes (ICS-039).

The tracker's own demand closes an error at its gain, which is too slow across
the sky: 15° takes 3 s to reach its field of view. A slew closes the error as
fast as the mount can stop in. Far from the target, the closing rate is the
one a steady deceleration brings to rest on it, the square root of twice the
deceleration times the error left; near it, where that would chatter, it is a
gain times the error. The two meet, with the same slope, where the error is
the deceleration over the gain squared.

As in the tracker's demand, the error is the target's angle less the mount's,
both at the time the demand takes effect, the shorter way round in azimuth.
The demand is the target's rate plus the closing rate, held within the axis's
rate limit.
"""

import math
from dataclasses import dataclass

from ics_models.handoff.approach import HandoffError
from ics_models.pointing.model import AltAz, wrap_pi
from ics_models.tracker.demand import AxisState
from ics_models.tracker.estimator import NS_PER_S


@dataclass(frozen=True)
class SlewLimits:
    """What a slew may ask of an axis.

    The rate is the fastest an axis turns, in rad/s. The acceleration is the
    deceleration the approach is planned on, in rad/s², which must be within
    what the mount can do. The gain, a second, takes over near the target.
    """

    rate_rad_s: float
    acceleration_rad_s2: float
    gain: float

    def __post_init__(self) -> None:
        """Refuse limits that are not all positive."""
        values = (self.rate_rad_s, self.acceleration_rad_s2, self.gain)
        if not all(math.isfinite(value) and value > 0.0 for value in values):
            message = f"a slew cannot be made within {values}"
            raise HandoffError(message)


def closing_rate(error: float, limits: SlewLimits) -> float:
    """The rate that closes an error, towards it: a gain times it when small, a steady deceleration's when not."""
    size = abs(error)
    knee = limits.acceleration_rad_s2 / (limits.gain * limits.gain)
    if size <= knee:
        return limits.gain * error
    return math.copysign(math.sqrt(2.0 * limits.acceleration_rad_s2 * (size - knee / 2.0)), error)


def held(rate: float, limit: float) -> float:
    """A rate held within a limit either way."""
    return min(max(rate, -limit), limit)


def slew_demand(target: AxisState, mount: AxisState, limits: SlewLimits) -> tuple[float, float]:
    """The azimuth and elevation rates for a target at the time a demand takes effect, from the mount as last seen."""
    lead = (target.time_ns - mount.time_ns) / NS_PER_S
    azimuth_error = wrap_pi(target.azimuth - (mount.azimuth + mount.azimuth_rate * lead))
    elevation_error = target.elevation - (mount.elevation + mount.elevation_rate * lead)
    azimuth_rate = target.azimuth_rate + closing_rate(azimuth_error, limits)
    elevation_rate = target.elevation_rate + closing_rate(elevation_error, limits)
    return held(azimuth_rate, limits.rate_rad_s), held(elevation_rate, limits.rate_rad_s)


def slew_seconds(angle: float, limits: SlewLimits) -> float:
    """How long an axis takes over an angle, from rest to rest, at the limits' rate and acceleration."""
    size = abs(angle)
    if size * limits.acceleration_rad_s2 <= limits.rate_rad_s * limits.rate_rad_s:
        return 2.0 * math.sqrt(size / limits.acceleration_rad_s2)
    return size / limits.rate_rad_s + limits.rate_rad_s / limits.acceleration_rad_s2


def reach_seconds(first: AltAz, second: AltAz, limits: SlewLimits) -> float:
    """How long a mount takes from one direction to another: its slower axis's time."""
    azimuth = slew_seconds(wrap_pi(second.azimuth - first.azimuth), limits)
    return max(azimuth, slew_seconds(second.elevation - first.elevation, limits))
