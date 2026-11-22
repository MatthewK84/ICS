"""Target-board poses, and the closed-form seed for the intrinsics fit (ICS-042).

A board pose is a rotation vector (axis times angle, radians) and a
translation (metres), taking board points, which lie on the board's z = 0
plane, into the camera frame. The seed is Zhang's: a homography per view
(normalised DLT), the focal lengths and principal point from the
homographies with the skew held at zero, and each view's pose from its
homography. Distortion is seeded at zero.
"""

import math
from collections.abc import Sequence
from dataclasses import dataclass

import numpy as np
import numpy.typing as npt

from ics_models.camera.model import CameraError, Distortion, Intrinsics

Vector = npt.NDArray[np.float64]
Matrix = npt.NDArray[np.float64]

SMALL_ANGLE: float = 1e-12
MINIMUM_VIEWS: int = 3
DEGENERATE: float = 1e-14


@dataclass(frozen=True)
class Pose:
    """A board's pose: rotation vector in radians and translation in metres, board to camera."""

    rotation: tuple[float, float, float]
    translation: tuple[float, float, float]


@dataclass(frozen=True)
class View:
    """One view of the board: its points on the board in metres, and their pixels."""

    board: Matrix
    pixels: Matrix


def rotation_matrix(rotation: tuple[float, float, float]) -> Matrix:
    """Rodrigues' formula: the rotation matrix of a rotation vector."""
    angle = math.sqrt(rotation[0] ** 2 + rotation[1] ** 2 + rotation[2] ** 2)
    if angle < SMALL_ANGLE:
        return np.eye(3)
    kx, ky, kz = (component / angle for component in rotation)
    skew = np.array([[0.0, -kz, ky], [kz, 0.0, -kx], [-ky, kx, 0.0]])
    matrix: Matrix = np.eye(3) + math.sin(angle) * skew + (1.0 - math.cos(angle)) * (skew @ skew)
    return matrix


def rotation_vector(matrix: Matrix) -> tuple[float, float, float]:
    """The rotation vector of a rotation matrix whose angle is under π."""
    cosine = max(-1.0, min(1.0, (float(np.trace(matrix)) - 1.0) / 2.0))
    angle = math.acos(cosine)
    axis = (
        float(matrix[2, 1] - matrix[1, 2]),
        float(matrix[0, 2] - matrix[2, 0]),
        float(matrix[1, 0] - matrix[0, 1]),
    )
    if angle < SMALL_ANGLE:
        return (axis[0] / 2.0, axis[1] / 2.0, axis[2] / 2.0)
    scale = angle / (2.0 * math.sin(angle))
    return (axis[0] * scale, axis[1] * scale, axis[2] * scale)


def normaliser(points: Matrix) -> Matrix:
    """Hartley's normalisation: centre the points and scale their mean distance to √2."""
    centre = points.mean(axis=0)
    spread = float(np.mean(np.hypot(points[:, 0] - centre[0], points[:, 1] - centre[1])))
    if spread == 0.0:
        raise CameraError("view", "has all its points in one place")
    scale = math.sqrt(2.0) / spread
    return np.array([[scale, 0.0, -scale * centre[0]], [0.0, scale, -scale * centre[1]], [0.0, 0.0, 1.0]])


def homography(view: View) -> Matrix:
    """The homography from board (x, y) to pixel, by normalised DLT."""
    from_board, from_pixels = normaliser(view.board), normaliser(view.pixels)
    count = view.board.shape[0]
    board = (from_board @ np.column_stack([view.board[:, :2], np.ones(count)]).T).T
    pixels = (from_pixels @ np.column_stack([view.pixels, np.ones(count)]).T).T
    rows = np.zeros((2 * count, 9))
    rows[0::2, 0:3] = board
    rows[0::2, 6:9] = -pixels[:, 0:1] * board
    rows[1::2, 3:6] = board
    rows[1::2, 6:9] = -pixels[:, 1:2] * board
    normalised = np.linalg.svd(rows)[2][-1].reshape(3, 3)
    found: Matrix = np.linalg.inv(from_pixels) @ normalised @ from_board
    scaled: Matrix = found / found[2, 2]
    return scaled


def constraint(h: Matrix, i: int, j: int) -> Vector:
    """Zhang's v_ij: h_i' B h_j as a row against B's six distinct entries."""
    return np.array(
        [
            h[0, i] * h[0, j],
            h[0, i] * h[1, j] + h[1, i] * h[0, j],
            h[1, i] * h[1, j],
            h[2, i] * h[0, j] + h[0, i] * h[2, j],
            h[2, i] * h[1, j] + h[1, i] * h[2, j],
            h[2, i] * h[2, j],
        ]
    )


def seed_camera(homographies: Sequence[Matrix], width: int, height: int) -> Intrinsics:
    """Zhang's closed form, with the skew held at zero."""
    if len(homographies) < MINIMUM_VIEWS:
        raise CameraError("calibration", f"needs at least {MINIMUM_VIEWS} views, not {len(homographies)}")
    rows = [np.array([0.0, 1.0, 0.0, 0.0, 0.0, 0.0])]
    for h in homographies:
        rows.extend([constraint(h, 0, 1), constraint(h, 0, 0) - constraint(h, 1, 1)])
    b11, b12, b22, b13, b23, b33 = np.linalg.svd(np.array(rows))[2][-1]
    denominator = b11 * b22 - b12 * b12
    too_alike = CameraError("calibration", "views too alike to seed the focal lengths: tilt the board more")
    # b is a unit vector; views square on to the camera leave B's focal entries at rounding noise.
    if abs(b11) < DEGENERATE or abs(denominator) < DEGENERATE * DEGENERATE:
        raise too_alike
    v0 = (b12 * b13 - b11 * b23) / denominator
    scale = b33 - (b13 * b13 + v0 * (b12 * b13 - b11 * b23)) / b11
    if not (scale / b11 > 0.0 and scale * b11 / denominator > 0.0):
        raise too_alike
    fx = math.sqrt(scale / b11)
    fy = math.sqrt(scale * b11 / denominator)
    u0 = -b13 * fx * fx / scale
    return Intrinsics(width=width, height=height, fx=fx, fy=fy, cx=float(u0), cy=float(v0), distortion=Distortion())


def seed_pose(h: Matrix, intrinsics: Intrinsics) -> Pose:
    """A view's pose from its homography, with the board in front of the camera."""
    inverse = np.linalg.inv(
        np.array([[intrinsics.fx, 0.0, intrinsics.cx], [0.0, intrinsics.fy, intrinsics.cy], [0.0, 0.0, 1.0]])
    )
    columns = inverse @ h
    scale = 1.0 / float(np.linalg.norm(columns[:, 0]))
    if columns[2, 2] < 0.0:
        scale = -scale
    r1, r2, translation = scale * columns[:, 0], scale * columns[:, 1], scale * columns[:, 2]
    left, _, right = np.linalg.svd(np.column_stack([r1, r2, np.cross(r1, r2)]))
    return Pose(rotation=rotation_vector(left @ right), translation=(translation[0], translation[1], translation[2]))


def board_in_camera(board: Matrix, pose: Pose) -> Matrix:
    """Board points in the camera frame."""
    matrix = rotation_matrix(pose.rotation)
    return np.column_stack(
        [matrix[row, 0] * board[:, 0] + matrix[row, 1] * board[:, 1] + pose.translation[row] for row in range(3)]
    )
