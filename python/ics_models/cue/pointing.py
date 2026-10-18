"""A station's cue: azimuth, elevation, their rates and the pointing uncertainty (ICS-037).

The cue is in the station's own ENU axes. Azimuth is clockwise from north,
in [0, 2π); elevation is above the station's horizontal. The rates come from
the estimated velocity, with the station fixed. The pointing uncertainty is the
one-sigma angle of the position covariance across the line of sight, along its
larger axis.
"""

import math
from dataclasses import dataclass

import numpy as np

from ics_models.cue.filter import CueError
from ics_models.cue.geometry import Geodetic, axes_between, to_enu
from ics_models.cue.kinematics import AXES, Estimate, Matrix, Vector

# Nearer than this, horizontally or in all, the direction is not defined well enough to cue.
MIN_HORIZONTAL_M: float = 0.01
MIN_RANGE_M: float = 1.0


@dataclass(frozen=True)
class Station:
    """A station's position in the range ENU frame, and the rotation into its own ENU axes."""

    position: Vector
    axes: Matrix


@dataclass(frozen=True)
class Cue:
    """Where a station points at a UTC time, in nanoseconds."""

    time_ns: int
    azimuth_rad: float
    elevation_rad: float
    azimuth_rate_rad_s: float
    elevation_rate_rad_s: float
    range_m: float
    sigma_rad: float


def station_at(origin: Geodetic, site: Geodetic) -> Station:
    """The station at a site, for the range ENU frame at origin."""
    return Station(to_enu(origin, site), axes_between(origin, site))


def wrap_azimuth(angle: float) -> float:
    """An angle from atan2, in [-π, π], as an azimuth in [0, 2π).

    A tiny negative angle plus 2π rounds to 2π itself, which is due north: 0.
    """
    turned = angle + 2.0 * math.pi if angle < 0.0 else angle
    return 0.0 if turned >= 2.0 * math.pi else turned


def transverse_sigma(covariance: Matrix, azimuth: float, elevation: float) -> float:
    """The one-sigma position error across the line of sight, along its larger axis, in metres."""
    along_azimuth = np.array([math.cos(azimuth), -math.sin(azimuth), 0.0])
    along_elevation = np.array(
        [-math.sin(azimuth) * math.sin(elevation), -math.cos(azimuth) * math.sin(elevation), math.cos(elevation)]
    )
    first = float(along_azimuth @ covariance @ along_azimuth)
    second = float(along_elevation @ covariance @ along_elevation)
    cross = float(along_azimuth @ covariance @ along_elevation)
    half_difference = (first - second) / 2.0
    largest = (first + second) / 2.0 + math.sqrt(half_difference * half_difference + cross * cross)
    return math.sqrt(max(largest, 0.0))


def cue_for(estimate: Estimate, station: Station) -> Cue:
    """The station's cue for an estimate."""
    east, north, up = station.axes @ (estimate.state[0:AXES] - station.position)
    v_east, v_north, v_up = station.axes @ estimate.state[AXES : 2 * AXES]
    horizontal = math.hypot(east, north)
    distance = math.hypot(horizontal, up)
    if horizontal < MIN_HORIZONTAL_M or distance < MIN_RANGE_M:
        message = f"the target is {horizontal} m from the station's vertical and {distance} m away: no direction"
        raise CueError(message)
    azimuth = wrap_azimuth(math.atan2(east, north))
    elevation = math.atan2(up, horizontal)
    azimuth_rate = (north * v_east - east * v_north) / (horizontal * horizontal)
    elevation_rate = (horizontal * horizontal * v_up - up * (east * v_east + north * v_north)) / (
        horizontal * distance * distance
    )
    covariance = station.axes @ estimate.covariance[0:AXES, 0:AXES] @ station.axes.T
    sigma = transverse_sigma(covariance, azimuth, elevation) / distance
    return Cue(estimate.time_ns, azimuth, elevation, azimuth_rate, elevation_rate, distance, sigma)
