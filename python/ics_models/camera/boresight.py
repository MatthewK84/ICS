"""The camera's boresight on the mount (ICS-042).

The camera rides the mount's elevation axis. Its nominal frame lines up with
the mount's line of sight: z along the line of sight, x toward increasing
azimuth and y toward decreasing elevation, so the image's right is the sky's
increasing azimuth and its down is the sky's decreasing elevation, as
ics_models.tracker.geometry has it. A mirroring camera turns x round. The
boresight is the small rotation from that frame to the camera's own:

    camera = Rz(roll) Ry(yaw) Rx(pitch) M nominal

with M the mirror, so pitch tips the optical axis up or down the image, yaw
across it, and roll turns the image about it. Fitting the boresight with the
intrinsics held also takes up the principal point's error: what matters is
which pixel a direction lands on.
"""

import math
from dataclasses import dataclass

from ics_models.camera.model import CameraError, Intrinsics, project, undistort
from ics_models.pointing.model import AltAz
from ics_models.tracker.geometry import basis_at, dot, wrap_azimuth

Rotation = tuple[tuple[float, float, float], tuple[float, float, float], tuple[float, float, float]]
Triple = tuple[float, float, float]


@dataclass(frozen=True)
class Boresight:
    """The camera's rotation off the mount's line of sight, in radians, and whether its optics mirror."""

    yaw: float
    pitch: float
    roll: float
    mirrored: bool


@dataclass(frozen=True)
class Camera:
    """A camera on the mount: its intrinsics and its boresight."""

    intrinsics: Intrinsics
    boresight: Boresight


def rotation(boresight: Boresight) -> Rotation:
    """Rz(roll) Ry(yaw) Rx(pitch), row by row."""
    cr, sr = math.cos(boresight.roll), math.sin(boresight.roll)
    cy, sy = math.cos(boresight.yaw), math.sin(boresight.yaw)
    cp, sp = math.cos(boresight.pitch), math.sin(boresight.pitch)
    return (
        (cr * cy, cr * sy * sp - sr * cp, cr * sy * cp + sr * sp),
        (sr * cy, sr * sy * sp + cr * cp, sr * sy * cp - cr * sp),
        (-sy, cy * sp, cy * cp),
    )


def rotate(matrix: Rotation, vector: Triple) -> Triple:
    """matrix times vector, summed in order so the last bit is the same on every machine."""
    return (
        matrix[0][0] * vector[0] + matrix[0][1] * vector[1] + matrix[0][2] * vector[2],
        matrix[1][0] * vector[0] + matrix[1][1] * vector[1] + matrix[1][2] * vector[2],
        matrix[2][0] * vector[0] + matrix[2][1] * vector[1] + matrix[2][2] * vector[2],
    )


def rotate_back(matrix: Rotation, vector: Triple) -> Triple:
    """matrix's transpose times vector."""
    return (
        matrix[0][0] * vector[0] + matrix[1][0] * vector[1] + matrix[2][0] * vector[2],
        matrix[0][1] * vector[0] + matrix[1][1] * vector[1] + matrix[2][1] * vector[2],
        matrix[0][2] * vector[0] + matrix[1][2] * vector[1] + matrix[2][2] * vector[2],
    )


def camera_vector(direction: AltAz, line_of_sight: AltAz, boresight: Boresight) -> Triple:
    """A direction on the sky in the camera's frame, with the mount's line of sight at the frame's time."""
    sight, along_azimuth, along_elevation = basis_at(line_of_sight)
    target = basis_at(direction)[0]
    across = dot(target, along_azimuth)
    nominal = (-across if boresight.mirrored else across, -dot(target, along_elevation), dot(target, sight))
    return rotate(rotation(boresight), nominal)


def pixel_of_sky(direction: AltAz, line_of_sight: AltAz, camera: Camera) -> tuple[float, float]:
    """The pixel a direction falls on; it must be in front of the camera."""
    vector = camera_vector(direction, line_of_sight, camera.boresight)
    if not vector[2] > 0.0:
        what, why = f"direction {direction}", f"is not in front of the camera at {line_of_sight}"
        raise CameraError(what, why)
    return project(vector, camera.intrinsics)


def sky_of_pixel(u: float, v: float, line_of_sight: AltAz, camera: Camera) -> AltAz:
    """The direction a pixel looks in, with the mount's line of sight at the frame's time."""
    x, y = undistort(u, v, camera.intrinsics)
    across, down, forward = rotate_back(rotation(camera.boresight), (x, y, 1.0))
    if camera.boresight.mirrored:
        across = -across
    sight, along_azimuth, along_elevation = basis_at(line_of_sight)
    east, north, up = forward * sight + across * along_azimuth - down * along_elevation
    return AltAz(wrap_azimuth(math.atan2(east, north)), math.atan2(up, math.hypot(east, north)))
