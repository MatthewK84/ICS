"""The camera fits, and the held-out check (ICS-042, C3).

The camera is fitted in three steps:

1. the intrinsics fit seeds from Zhang's closed form, then fits the focal
   lengths, principal point, distortion and every view's board pose to the
   target points;
2. the boresight fit holds those intrinsics and fits the three angles to the
   sightings;
3. the joint fit starts from both and fits everything to the target points
   and the sightings together.

The third step matters. A board a few tens of metres from a narrow camera
cannot fix its focal length: the fit trades the focal length for the board's
distance and still lands every board point. Sightings of directions, which
are at infinity, fix the angular scale.

A fit's RMS is the root mean square pixel distance between where the model
puts a point and where it was seen. C3 asks for at most 0.5 px on points held
out of the fit: target points, each with its view's fitted pose, and
sightings.
"""

import math
from collections.abc import Sequence
from dataclasses import dataclass

import numpy as np
import numpy.typing as npt

from ics_models.camera.boresight import Boresight, Camera, camera_vector, rotation
from ics_models.camera.model import CameraError, Distortion, Intrinsics, project_normalised
from ics_models.camera.observations import Sighting, TargetPoint, views_of
from ics_models.camera.pose import Pose, View, board_in_camera, homography, seed_camera, seed_pose
from ics_models.camera.solve import jacobian, solve, solve_free

Vector = npt.NDArray[np.float64]
Matrix = npt.NDArray[np.float64]

INTRINSIC_NAMES: tuple[str, ...] = ("fx", "fy", "cx", "cy", "k1", "k2", "k3", "p1", "p2")
DISTORTION_NAMES: tuple[str, ...] = INTRINSIC_NAMES[4:]
BORESIGHT_NAMES: tuple[str, ...] = ("yaw", "pitch", "roll")
POSE_SIZE: int = 6
FOCAL_STEP: float = 1e-6
PIXEL_STEP: float = 1e-3
DISTORTION_STEP: float = 1e-4
ANGLE_STEP: float = 1e-7
TRANSLATION_STEP: float = 1e-6


@dataclass(frozen=True)
class IntrinsicsFit:
    """The fitted intrinsics, each parameter's standard deviation in INTRINSIC_NAMES's order, and every view's pose."""

    intrinsics: Intrinsics
    sigmas: tuple[float, ...]
    poses: dict[str, Pose]
    rms_px: float
    points: int


@dataclass(frozen=True)
class Sky:
    """Sightings ready to fit: each direction in the camera's nominal frame, mirrored if it mirrors, and its pixel."""

    nominal: Matrix
    pixels: Matrix
    mirrored: bool


@dataclass(frozen=True)
class Calibration:
    """What the joint fit fits to: the views in order, the sightings and the image size."""

    views: tuple[View, ...]
    sky: Sky
    width: int
    height: int


@dataclass(frozen=True)
class CameraFit:
    """The camera fitted to target points and sightings together.

    sigmas follow INTRINSIC_NAMES, then BORESIGHT_NAMES.
    """

    camera: Camera
    sigmas: tuple[float, ...]
    poses: dict[str, Pose]
    target_rms_px: float
    sighting_rms_px: float
    points: int
    sightings: int


@dataclass(frozen=True)
class BoresightFit:
    """The fitted boresight, each angle's standard deviation in BORESIGHT_NAMES's order, and the fit's RMS."""

    boresight: Boresight
    sigmas: tuple[float, ...]
    rms_px: float
    sightings: int


def intrinsics_of(state: Vector, width: int, height: int) -> Intrinsics:
    """The intrinsics in the first nine parameters."""
    values = [float(value) for value in state[: len(INTRINSIC_NAMES)]]
    distortion = Distortion(k1=values[4], k2=values[5], k3=values[6], p1=values[7], p2=values[8])
    return Intrinsics(width, height, values[0], values[1], values[2], values[3], distortion)


def intrinsics_vector(intrinsics: Intrinsics) -> list[float]:
    """The intrinsics as parameters, in INTRINSIC_NAMES's order."""
    d = intrinsics.distortion
    return [intrinsics.fx, intrinsics.fy, intrinsics.cx, intrinsics.cy, d.k1, d.k2, d.k3, d.p1, d.p2]


def pose_of(state: Vector, index: int) -> Pose:
    """The index-th view's pose, after the intrinsics."""
    start = len(INTRINSIC_NAMES) + POSE_SIZE * index
    values = [float(value) for value in state[start : start + POSE_SIZE]]
    return Pose(rotation=(values[0], values[1], values[2]), translation=(values[3], values[4], values[5]))


def view_pixels(view: View, pose: Pose, intrinsics: Intrinsics) -> Matrix:
    """Where the model puts a view's board points."""
    points = board_in_camera(view.board, pose)
    if not bool(np.all(points[:, 2] > 0.0)):
        raise CameraError("fit", "put a board point behind the camera")
    u, v = project_normalised(points[:, 0] / points[:, 2], points[:, 1] / points[:, 2], intrinsics)
    return np.column_stack([u, v])


def target_residuals(state: Vector, views: Sequence[View], width: int, height: int) -> Vector:
    """Every board point's pixel error, u then v, view by view."""
    intrinsics = intrinsics_of(state, width, height)
    errors = [(view_pixels(view, pose_of(state, i), intrinsics) - view.pixels).ravel() for i, view in enumerate(views)]
    return np.concatenate(errors)


def target_steps(state: Vector, views: int) -> Vector:
    """The finite-difference step for each parameter."""
    focal = FOCAL_STEP * max(abs(float(state[0])), abs(float(state[1])))
    head = [focal, focal, PIXEL_STEP, PIXEL_STEP] + [DISTORTION_STEP] * 5
    pose_steps: list[float] = []
    for index in range(views):
        distance = float(np.linalg.norm(state[len(INTRINSIC_NAMES) + POSE_SIZE * index + 3 :][:3]))
        pose_steps.extend([ANGLE_STEP] * 3 + [TRANSLATION_STEP * max(distance, 1.0)] * 3)
    return np.array(head + pose_steps)


def view_derivatives(state: Vector, view: View, index: int, steps: Vector, intrinsics: Intrinsics) -> Matrix:
    """A view's residuals' derivatives by its own pose, by central differences."""
    start = len(INTRINSIC_NAMES) + POSE_SIZE * index
    columns: list[Vector] = []
    for offset in range(POSE_SIZE):
        moved = np.zeros_like(state)
        moved[start + offset] = steps[start + offset]
        ahead = view_pixels(view, pose_of(state + moved, index), intrinsics).ravel()
        behind = view_pixels(view, pose_of(state - moved, index), intrinsics).ravel()
        columns.append((ahead - behind) / (2.0 * float(steps[start + offset])))
    return np.column_stack(columns)


def target_jacobian(state: Vector, views: Sequence[View], width: int, height: int) -> Matrix:
    """The residuals' derivatives: by the intrinsics for every point, and by each pose for its own view's only."""
    steps = target_steps(state, len(views))
    head = len(INTRINSIC_NAMES)
    poses = state[head:]
    by_intrinsics = jacobian(
        lambda s: target_residuals(np.concatenate([s, poses]), views, width, height), state[:head], steps[:head]
    )
    result = np.zeros((by_intrinsics.shape[0], state.size))
    result[:, :head] = by_intrinsics
    intrinsics = intrinsics_of(state, width, height)
    row = 0
    for index, view in enumerate(views):
        block = view_derivatives(state, view, index, steps, intrinsics)
        start = head + POSE_SIZE * index
        result[row : row + block.shape[0], start : start + POSE_SIZE] = block
        row += block.shape[0]
    return result


def free_mask(terms: Sequence[str], size: int) -> Vector:
    """Which parameters a fit frees: all but the distortion terms not named."""
    unknown = [term for term in terms if term not in DISTORTION_NAMES]
    if unknown:
        what, why = "distortion terms", "are not Brown-Conrady terms: " + ", ".join(unknown)
        raise CameraError(what, why)
    free = np.ones(size)
    for offset, name in enumerate(DISTORTION_NAMES):
        if name not in terms:
            free[4 + offset] = 0.0
    return free


def pixel_rms(errors: Vector) -> float:
    """The RMS pixel distance of residuals laid out u, v, u, v, ..."""
    return math.sqrt(2.0 * float(errors @ errors) / errors.size)


def fit_intrinsics(
    points: Sequence[TargetPoint], width: int, height: int, terms: Sequence[str] = DISTORTION_NAMES
) -> IntrinsicsFit:
    """The intrinsics, the distortion terms named (the rest held at zero) and board poses that best fit the points."""
    views = views_of(points)
    homographies = [homography(view) for view in views.values()]
    seed = seed_camera(homographies, width, height)
    state = intrinsics_vector(seed)
    for h in homographies:
        pose = seed_pose(h, seed)
        state.extend([*pose.rotation, *pose.translation])
    ordered = list(views.values())
    solution = solve_free(
        lambda s: target_residuals(s, ordered, width, height),
        lambda s: target_jacobian(s, ordered, width, height),
        np.array(state),
        free_mask(terms, len(state)),
    )
    sigmas = tuple(float(math.sqrt(solution.covariance[i, i])) for i in range(len(INTRINSIC_NAMES)))
    poses = {name: pose_of(solution.state, index) for index, name in enumerate(views)}
    rms = pixel_rms(target_residuals(solution.state, ordered, width, height))
    return IntrinsicsFit(intrinsics_of(solution.state, width, height), sigmas, poses, rms, len(points))


def target_rms(points: Sequence[TargetPoint], fit: IntrinsicsFit) -> float:
    """The RMS pixel distance of target points from the fit, each with its view's fitted pose (C3 when held out)."""
    views = views_of(points)
    unknown = [name for name in views if name not in fit.poses]
    if unknown:
        what, why = "held-out points", "are in views the fit did not see: " + ", ".join(unknown)
        raise CameraError(what, why)
    errors = [
        (view_pixels(view, fit.poses[name], fit.intrinsics) - view.pixels).ravel() for name, view in views.items()
    ]
    return pixel_rms(np.concatenate(errors))


BORESIGHT_START: int = len(INTRINSIC_NAMES)
JOINT_HEAD: int = len(INTRINSIC_NAMES) + len(BORESIGHT_NAMES)


def sky_of(sightings: Sequence[Sighting], mirrored: bool) -> Sky:
    """Sightings ready to fit."""
    if not sightings:
        raise CameraError("fit", "needs sightings")
    still = Boresight(0.0, 0.0, 0.0, mirrored)
    nominal = np.array([camera_vector(s.target, s.line_of_sight, still) for s in sightings])
    return Sky(nominal, np.array([[s.u_px, s.v_px] for s in sightings]), mirrored)


def boresight_of(angles: Vector, mirrored: bool) -> Boresight:
    """The boresight in three angles: yaw, pitch, roll."""
    return Boresight(float(angles[0]), float(angles[1]), float(angles[2]), mirrored)


def sky_residuals(angles: Vector, sky: Sky, intrinsics: Intrinsics) -> Vector:
    """Every sighting's pixel error, u then v."""
    matrix = rotation(boresight_of(angles, sky.mirrored))
    n = sky.nominal
    turned = [matrix[row][0] * n[:, 0] + matrix[row][1] * n[:, 1] + matrix[row][2] * n[:, 2] for row in range(3)]
    if not bool(np.all(turned[2] > 0.0)):
        raise CameraError("fit", "put a sighting behind the camera")
    u, v = project_normalised(turned[0] / turned[2], turned[1] / turned[2], intrinsics)
    errors: Vector = (np.column_stack([u, v]) - sky.pixels).ravel()
    return errors


def fit_boresight(sightings: Sequence[Sighting], intrinsics: Intrinsics, mirrored: bool) -> BoresightFit:
    """The boresight that best fits the sightings, with the intrinsics held."""
    sky = sky_of(sightings, mirrored)

    def residuals(angles: Vector) -> Vector:
        return sky_residuals(angles, sky, intrinsics)

    steps = np.full(len(BORESIGHT_NAMES), ANGLE_STEP)
    solution = solve(residuals, lambda a: jacobian(residuals, a, steps), np.zeros(len(BORESIGHT_NAMES)))
    sigmas = tuple(float(math.sqrt(solution.covariance[i, i])) for i in range(len(BORESIGHT_NAMES)))
    rms = pixel_rms(residuals(solution.state))
    return BoresightFit(boresight_of(solution.state, mirrored), sigmas, rms, len(sightings))


def sighting_rms(sightings: Sequence[Sighting], camera: Camera) -> float:
    """The RMS pixel distance of sightings from the camera model (C3 when held out)."""
    b = camera.boresight
    sky = sky_of(sightings, b.mirrored)
    return pixel_rms(sky_residuals(np.array([b.yaw, b.pitch, b.roll]), sky, camera.intrinsics))


def split_state(state: Vector) -> tuple[Vector, Vector]:
    """A joint state as the target fit's (intrinsics, then poses) and the boresight's angles."""
    return np.concatenate([state[:BORESIGHT_START], state[JOINT_HEAD:]]), state[BORESIGHT_START:JOINT_HEAD]


def joint_residuals(state: Vector, calibration: Calibration) -> Vector:
    """Every target point's pixel error, then every sighting's."""
    targets, angles = split_state(state)
    intrinsics = intrinsics_of(targets, calibration.width, calibration.height)
    on_board = target_residuals(targets, calibration.views, calibration.width, calibration.height)
    return np.concatenate([on_board, sky_residuals(angles, calibration.sky, intrinsics)])


def sky_jacobian(state: Vector, calibration: Calibration) -> Matrix:
    """The sightings' residuals' derivatives by the intrinsics and the boresight; the poses do not touch them."""
    width, height = calibration.width, calibration.height
    steps = np.concatenate([target_steps(state, 0)[:BORESIGHT_START], np.full(len(BORESIGHT_NAMES), ANGLE_STEP)])

    def residuals(head: Vector) -> Vector:
        intrinsics = intrinsics_of(head, width, height)
        return sky_residuals(head[BORESIGHT_START:], calibration.sky, intrinsics)

    return jacobian(residuals, state[:JOINT_HEAD], steps)


def joint_jacobian(state: Vector, calibration: Calibration) -> Matrix:
    """The residuals' derivatives: the intrinsics touch every residual, the boresight the sightings' only."""
    targets, _ = split_state(state)
    on_board = target_jacobian(targets, calibration.views, calibration.width, calibration.height)
    on_sky = sky_jacobian(state, calibration)
    rows = on_board.shape[0]
    result = np.zeros((rows + on_sky.shape[0], state.size))
    result[:rows, :BORESIGHT_START] = on_board[:, :BORESIGHT_START]
    result[:rows, JOINT_HEAD:] = on_board[:, BORESIGHT_START:]
    result[rows:, :JOINT_HEAD] = on_sky
    return result


@dataclass(frozen=True)
class Setup:
    """The camera being fitted: its image size, whether its optics mirror, and the distortion terms to fit."""

    width: int
    height: int
    mirrored: bool
    terms: tuple[str, ...] = DISTORTION_NAMES


def fit_camera(points: Sequence[TargetPoint], sightings: Sequence[Sighting], setup: Setup) -> CameraFit:
    """The camera that best fits the target points and the sightings together."""
    width, height, mirrored = setup.width, setup.height, setup.mirrored
    views = views_of(points)
    alone = fit_intrinsics(points, width, height, setup.terms)
    bore = fit_boresight(sightings, alone.intrinsics, mirrored).boresight
    seed = [*intrinsics_vector(alone.intrinsics), bore.yaw, bore.pitch, bore.roll]
    for name in views:
        seed.extend([*alone.poses[name].rotation, *alone.poses[name].translation])
    calibration = Calibration(tuple(views.values()), sky_of(sightings, mirrored), width, height)
    solution = solve_free(
        lambda s: joint_residuals(s, calibration),
        lambda s: joint_jacobian(s, calibration),
        np.array(seed),
        free_mask(setup.terms, len(seed)),
    )
    targets, angles = split_state(solution.state)
    camera = Camera(intrinsics_of(targets, width, height), boresight_of(angles, mirrored))
    sigmas = tuple(float(math.sqrt(solution.covariance[i, i])) for i in range(JOINT_HEAD))
    poses = {name: pose_of(targets, index) for index, name in enumerate(views)}
    on_board = pixel_rms(target_residuals(targets, calibration.views, width, height))
    on_sky = pixel_rms(sky_residuals(angles, calibration.sky, camera.intrinsics))
    return CameraFit(camera, sigmas, poses, on_board, on_sky, len(points), len(sightings))


def camera_target_rms(points: Sequence[TargetPoint], fit: CameraFit) -> float:
    """The RMS pixel distance of target points from a joint fit, each with its view's fitted pose."""
    return target_rms(
        points, IntrinsicsFit(fit.camera.intrinsics, fit.sigmas, fit.poses, fit.target_rms_px, fit.points)
    )
