"""The axis rates that follow the target and close the pointing error (ICS-038).

A demand takes effect a lead time after it is made. By then the mount has
moved on at its own rates, and the target is where its state predicts. The
demand is the target's rate plus gain times the remaining error, in the
mount's own axis angles (the shorter way round in azimuth). The target's
rates on the sky stand in for its axis rates: the pointing model turns them by
well under a thousandth.
"""

from dataclasses import dataclass

from ics_models.pointing.model import wrap_pi
from ics_models.tracker.centroid import TrackerError
from ics_models.tracker.estimator import NS_PER_S


@dataclass(frozen=True)
class AxisState:
    """Axis angles and rates, in radians and radians a second, at a UTC time in nanoseconds."""

    time_ns: int
    azimuth: float
    elevation: float
    azimuth_rate: float
    elevation_rate: float


def demand(target: AxisState, mount: AxisState, gain: float) -> tuple[float, float]:
    """The azimuth and elevation rates for a target at the time a demand takes effect, from the mount as last seen."""
    if gain < 0.0:
        message = f"a gain of {gain} per second would push the error up"
        raise TrackerError(message)
    lead = (target.time_ns - mount.time_ns) / NS_PER_S
    azimuth_error = wrap_pi(target.azimuth - (mount.azimuth + mount.azimuth_rate * lead))
    elevation_error = target.elevation - (mount.elevation + mount.elevation_rate * lead)
    return target.azimuth_rate + gain * azimuth_error, target.elevation_rate + gain * elevation_error
