"""Distances between vehicles for the SITL rig's checks (ICS-018).

Positions are WGS84 latitude and longitude with a height above the range's
ground level, which both vehicles' homes share. Converting each to Earth-
centred, Earth-fixed coordinates with that height in place of the ellipsoid
height gives the straight-line distance between them: the ground level's own
height above the ellipsoid is common to both, and over a range a few
kilometres across it moves a distance by far less than a millimetre.
"""

from __future__ import annotations

import math
from dataclasses import dataclass
from typing import Final

SEMI_MAJOR_AXIS_M: Final = 6_378_137.0
FLATTENING: Final = 1.0 / 298.257223563
ECCENTRICITY_SQUARED: Final = FLATTENING * (2.0 - FLATTENING)


@dataclass(frozen=True)
class Position:
    """A vehicle position: degrees, and metres above the range's ground level."""

    latitude_deg: float
    longitude_deg: float
    height_m: float


def ecef(position: Position) -> tuple[float, float, float]:
    """Earth-centred, Earth-fixed coordinates in metres."""
    latitude = math.radians(position.latitude_deg)
    longitude = math.radians(position.longitude_deg)
    sin_latitude = math.sin(latitude)
    normal = SEMI_MAJOR_AXIS_M / math.sqrt(1.0 - ECCENTRICITY_SQUARED * sin_latitude * sin_latitude)
    across = (normal + position.height_m) * math.cos(latitude)
    return (
        across * math.cos(longitude),
        across * math.sin(longitude),
        (normal * (1.0 - ECCENTRICITY_SQUARED) + position.height_m) * sin_latitude,
    )


def distance_m(first: Position, second: Position) -> float:
    """The straight-line distance between two positions."""
    return math.dist(ecef(first), ecef(second))
