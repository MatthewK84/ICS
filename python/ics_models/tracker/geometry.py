"""A pixel's direction on the sky, and a direction's pixel (ICS-038).

The camera rides the mount's elevation axis, so its image keeps one
orientation to the mount's axes. Its boresight is the pixel the mount's line
of sight falls on. A pixel's offset from it, with y turned to point up,
mirrored when the optics mirror the image, rotated by the roll and scaled by
the plate scale, is a point on the plane tangent to the sky at the line of
sight: xi toward increasing azimuth, eta toward increasing elevation. The
gnomonic projection takes that point to a direction, and back.
"""

import math
from dataclasses import dataclass

import numpy as np
import numpy.typing as npt

from ics_models.pointing.model import AltAz
from ics_models.tracker.centroid import TrackerError

Vector = npt.NDArray[np.float64]

TWO_PI: float = 2.0 * math.pi


@dataclass(frozen=True)
class CameraGeometry:
    """The camera on the mount: boresight pixel, radians a pixel, roll in radians, and whether it mirrors."""

    boresight_x: float
    boresight_y: float
    plate_scale: float
    roll: float
    mirrored: bool


def wrap_azimuth(angle: float) -> float:
    """An azimuth in [0, 2π). A tiny negative angle plus 2π rounds to 2π itself, which is due north: 0."""
    turned = angle % TWO_PI
    return 0.0 if turned >= TWO_PI else turned


def tangent_of_pixel(x: float, y: float, camera: CameraGeometry) -> tuple[float, float]:
    """A pixel's point on the tangent plane: xi toward increasing azimuth, eta toward increasing elevation."""
    right = x - camera.boresight_x
    up = camera.boresight_y - y
    if camera.mirrored:
        right = -right
    cos_roll, sin_roll = math.cos(camera.roll), math.sin(camera.roll)
    return (
        camera.plate_scale * (cos_roll * right - sin_roll * up),
        camera.plate_scale * (sin_roll * right + cos_roll * up),
    )


def pixel_of_tangent(xi: float, eta: float, camera: CameraGeometry) -> tuple[float, float]:
    """The pixel at a point on the tangent plane."""
    cos_roll, sin_roll = math.cos(camera.roll), math.sin(camera.roll)
    right = (cos_roll * xi + sin_roll * eta) / camera.plate_scale
    up = (-sin_roll * xi + cos_roll * eta) / camera.plate_scale
    if camera.mirrored:
        right = -right
    return camera.boresight_x + right, camera.boresight_y - up


def basis_at(line_of_sight: AltAz) -> tuple[Vector, Vector, Vector]:
    """The line of sight, and the directions of increasing azimuth and elevation there, in east, north, up."""
    sin_a, cos_a = math.sin(line_of_sight.azimuth), math.cos(line_of_sight.azimuth)
    sin_e, cos_e = math.sin(line_of_sight.elevation), math.cos(line_of_sight.elevation)
    return (
        np.array([cos_e * sin_a, cos_e * cos_a, sin_e]),
        np.array([cos_a, -sin_a, 0.0]),
        np.array([-sin_e * sin_a, -sin_e * cos_a, cos_e]),
    )


def sky_of_tangent(xi: float, eta: float, line_of_sight: AltAz) -> AltAz:
    """The direction of a point on the plane tangent to the sky at the line of sight."""
    sight, along_azimuth, along_elevation = basis_at(line_of_sight)
    east, north, up = sight + xi * along_azimuth + eta * along_elevation
    return AltAz(wrap_azimuth(math.atan2(east, north)), math.atan2(up, math.hypot(east, north)))


def tangent_of_sky(direction: AltAz, line_of_sight: AltAz) -> tuple[float, float]:
    """A direction's point on the plane tangent to the sky at the line of sight; it must be in front."""
    sight, along_azimuth, along_elevation = basis_at(line_of_sight)
    target = basis_at(direction)[0]
    depth = float(target @ sight)
    if depth <= 0.0:
        message = f"{direction} is 90 degrees or more from the line of sight {line_of_sight}"
        raise TrackerError(message)
    return float(target @ along_azimuth) / depth, float(target @ along_elevation) / depth


def sky_of_pixel(x: float, y: float, line_of_sight: AltAz, camera: CameraGeometry) -> AltAz:
    """The direction a pixel looks in, with the mount's line of sight at the frame's time."""
    xi, eta = tangent_of_pixel(x, y, camera)
    return sky_of_tangent(xi, eta, line_of_sight)


def pixel_of_sky(direction: AltAz, line_of_sight: AltAz, camera: CameraGeometry) -> tuple[float, float]:
    """The pixel a direction falls on, with the mount's line of sight at the frame's time."""
    xi, eta = tangent_of_sky(direction, line_of_sight)
    return pixel_of_tangent(xi, eta, camera)
