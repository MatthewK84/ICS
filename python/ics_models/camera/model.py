"""A camera's intrinsics and Brown-Conrady distortion (ICS-042).

The camera frame has x to the right of the image, y down and z out along the
optical axis. A point in front of the camera, at x/z and y/z on the normalised
image plane, is distorted with three radial terms (k1, k2, k3) and two
tangential ones (p1, p2), then scaled by the focal lengths in pixels and
shifted to the principal point. Pixel (0, 0) is the centre of the image's
top-left pixel. Undistortion inverts the distortion with Newton steps.
"""

import math
from dataclasses import dataclass

import numpy as np
import numpy.typing as npt

Vector = npt.NDArray[np.float64]

NEWTON_STEPS: int = 20
NEWTON_TOLERANCE: float = 1e-12


class CameraError(ValueError):
    """Input the camera model refuses: what was refused, and why."""

    def __init__(self, what: str, why: str) -> None:
        super().__init__(f"{what}: {why}")


@dataclass(frozen=True)
class Distortion:
    """Brown-Conrady distortion: radial k1, k2, k3 and tangential p1, p2."""

    k1: float = 0.0
    k2: float = 0.0
    k3: float = 0.0
    p1: float = 0.0
    p2: float = 0.0


@dataclass(frozen=True)
class Intrinsics:
    """The image's size in pixels, the focal lengths and principal point in pixels, and the distortion."""

    width: int
    height: int
    fx: float
    fy: float
    cx: float
    cy: float
    distortion: Distortion


def distort(x: Vector, y: Vector, distortion: Distortion) -> tuple[Vector, Vector]:
    """Normalised image points, distorted."""
    r2 = x * x + y * y
    radial = 1.0 + r2 * (distortion.k1 + r2 * (distortion.k2 + r2 * distortion.k3))
    xy = x * y
    distorted_x = x * radial + 2.0 * distortion.p1 * xy + distortion.p2 * (r2 + 2.0 * x * x)
    distorted_y = y * radial + distortion.p1 * (r2 + 2.0 * y * y) + 2.0 * distortion.p2 * xy
    return distorted_x, distorted_y


def project_normalised(x: Vector, y: Vector, intrinsics: Intrinsics) -> tuple[Vector, Vector]:
    """The pixels of normalised image points."""
    distorted_x, distorted_y = distort(x, y, intrinsics.distortion)
    return intrinsics.fx * distorted_x + intrinsics.cx, intrinsics.fy * distorted_y + intrinsics.cy


def project(point: tuple[float, float, float], intrinsics: Intrinsics) -> tuple[float, float]:
    """The pixel of a point in the camera frame; it must be in front of the camera."""
    x, y, z = point
    if not z > 0.0:
        what = f"point {point}"
        raise CameraError(what, "is not in front of the camera")
    u, v = project_normalised(np.array([x / z]), np.array([y / z]), intrinsics)
    return float(u[0]), float(v[0])


def distortion_jacobian(x: float, y: float, distortion: Distortion) -> tuple[float, float, float, float]:
    """The distortion's derivatives at a normalised point: d(xd)/dx, d(xd)/dy, d(yd)/dx, d(yd)/dy."""
    k1, k2, k3, p1, p2 = distortion.k1, distortion.k2, distortion.k3, distortion.p1, distortion.p2
    r2 = x * x + y * y
    radial = 1.0 + r2 * (k1 + r2 * (k2 + r2 * k3))
    slope = k1 + r2 * (2.0 * k2 + 3.0 * r2 * k3)
    return (
        radial + 2.0 * x * x * slope + 2.0 * p1 * y + 6.0 * p2 * x,
        2.0 * x * y * slope + 2.0 * p1 * x + 2.0 * p2 * y,
        2.0 * x * y * slope + 2.0 * p1 * x + 2.0 * p2 * y,
        radial + 2.0 * y * y * slope + 6.0 * p1 * y + 2.0 * p2 * x,
    )


def newton_step(x: float, y: float, target: tuple[float, float], distortion: Distortion) -> tuple[float, float]:
    """The Newton step from (x, y) toward the undistorted point whose distortion is target."""
    distorted_x, distorted_y = distort(np.array([x]), np.array([y]), distortion)
    error_x, error_y = float(distorted_x[0]) - target[0], float(distorted_y[0]) - target[1]
    a, b, c, d = distortion_jacobian(x, y, distortion)
    determinant = a * d - b * c
    if determinant == 0.0:
        what = f"normalised point ({x}, {y})"
        raise CameraError(what, "is where the distortion folds over")
    return (d * error_x - b * error_y) / determinant, (a * error_y - c * error_x) / determinant


def undistort(u: float, v: float, intrinsics: Intrinsics) -> tuple[float, float]:
    """The normalised image point, x/z and y/z, that projects to the pixel."""
    target = ((u - intrinsics.cx) / intrinsics.fx, (v - intrinsics.cy) / intrinsics.fy)
    x, y = target
    for _ in range(NEWTON_STEPS):
        step_x, step_y = newton_step(x, y, target, intrinsics.distortion)
        x, y = x - step_x, y - step_y
        if math.hypot(step_x, step_y) < NEWTON_TOLERANCE:
            return x, y
    what, why = f"pixel ({u}, {v})", f"did not undistort in {NEWTON_STEPS} Newton steps"
    raise CameraError(what, why)
