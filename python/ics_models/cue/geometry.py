"""WGS 84 positions in the range ENU frame, and station axes (ICS-037).

The range ENU frame is ICS-017's (docs/frames-and-time.md): origin at a
geodetic point, up along the ellipsoid normal there. Each station points in
its own ENU axes, which differ from the range's by the change in the normal,
about 50 µrad at 300 m.
"""

import math
from dataclasses import dataclass

import numpy as np

from ics_models.cue.kinematics import Matrix, Vector

SEMI_MAJOR_AXIS_M: float = 6378137.0
FLATTENING: float = 1.0 / 298.257223563
ECCENTRICITY_SQUARED: float = FLATTENING * (2.0 - FLATTENING)


@dataclass(frozen=True)
class Geodetic:
    """A WGS 84 point: latitude and longitude in degrees, height above the ellipsoid in metres."""

    latitude_deg: float
    longitude_deg: float
    height_m: float


def to_ecef(point: Geodetic) -> Vector:
    """The ECEF position of a geodetic point, in metres."""
    latitude = math.radians(point.latitude_deg)
    longitude = math.radians(point.longitude_deg)
    sin_latitude = math.sin(latitude)
    normal = SEMI_MAJOR_AXIS_M / math.sqrt(1.0 - ECCENTRICITY_SQUARED * sin_latitude * sin_latitude)
    horizontal = (normal + point.height_m) * math.cos(latitude)
    return np.array(
        [
            horizontal * math.cos(longitude),
            horizontal * math.sin(longitude),
            (normal * (1.0 - ECCENTRICITY_SQUARED) + point.height_m) * sin_latitude,
        ]
    )


def enu_axes(point: Geodetic) -> Matrix:
    """The rotation from ECEF axes to the ENU axes at a point: its rows are east, north and up."""
    latitude = math.radians(point.latitude_deg)
    longitude = math.radians(point.longitude_deg)
    sin_lat, cos_lat = math.sin(latitude), math.cos(latitude)
    sin_lon, cos_lon = math.sin(longitude), math.cos(longitude)
    return np.array(
        [
            [-sin_lon, cos_lon, 0.0],
            [-sin_lat * cos_lon, -sin_lat * sin_lon, cos_lat],
            [cos_lat * cos_lon, cos_lat * sin_lon, sin_lat],
        ]
    )


def to_enu(origin: Geodetic, point: Geodetic) -> Vector:
    """A point's position in the ENU frame at origin, in metres."""
    return enu_axes(origin) @ (to_ecef(point) - to_ecef(origin))


def axes_between(origin: Geodetic, point: Geodetic) -> Matrix:
    """The rotation from the ENU axes at origin to those at point."""
    return enu_axes(point) @ enu_axes(origin).T
