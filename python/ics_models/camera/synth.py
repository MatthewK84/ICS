"""Synthetic calibrations through a known camera, to prove the fits (ICS-042).

The three cameras here are made up but plausible: a narrow camera of
1280 x 800 pixels at about 50 µrad a pixel, a wide one of 1280 x 800 at
about 400 µrad, and an MWIR one of 640 x 512 at about 150 µrad. Real values
only ever come from fitted parameter files. The narrow and MWIR cameras
have only radial k1 distortion.

Each view puts a flat board of dots, a metre across, a little off the
optical axis and tilted 20° to 45° about a random axis, at the distance where
it fills about 60% of the image's width. Points that fall off the image are
dropped. Each sighting looks along a random line of sight at a direction
that lands on a random pixel. Pixels carry Gaussian noise on each axis.
"""

import math
from dataclasses import dataclass

import numpy as np

from ics_models.camera.boresight import Boresight, Camera, sky_of_pixel
from ics_models.camera.fit import view_pixels
from ics_models.camera.model import CameraError, Distortion, Intrinsics
from ics_models.camera.observations import Sighting, TargetPoint
from ics_models.camera.pose import Pose, View, rotation_vector
from ics_models.pointing.model import AltAz

BOARD_WIDTH_M: float = 1.0
BOARD_COLUMNS: int = 11
BOARD_ROWS: int = 8
FILL: float = 0.6
LEAST_TILT_RAD: float = math.radians(20.0)
MOST_TILT_RAD: float = math.radians(45.0)
OFFSET: float = 0.2
LOWEST_RAD: float = math.radians(10.0)
HIGHEST_RAD: float = math.radians(70.0)

NARROW: Camera = Camera(
    Intrinsics(1280, 800, 20000.0, 20010.0, 645.3, 396.8, Distortion(k1=0.8)),
    Boresight(yaw=1.5e-3, pitch=-8e-4, roll=2e-3, mirrored=False),
)
WIDE: Camera = Camera(
    Intrinsics(1280, 800, 2500.0, 2498.0, 636.2, 405.1, Distortion(-0.21, 0.09, -0.012, 3e-4, -2e-4)),
    Boresight(yaw=-4e-3, pitch=6e-3, roll=-5e-3, mirrored=True),
)
MWIR: Camera = Camera(
    Intrinsics(640, 512, 6667.0, 6660.0, 322.4, 250.9, Distortion(k1=-0.35)),
    Boresight(yaw=2.5e-3, pitch=1.2e-3, roll=-3e-3, mirrored=False),
)
CAMERAS: tuple[tuple[str, Camera], ...] = (("narrow", NARROW), ("wide", WIDE), ("mwir", MWIR))
# The distortion terms worth fitting to each: across a narrow field, k2, k3
# and the tangential terms move a pixel by hundredths of a pixel, and a fit
# cannot find them.
TERMS: tuple[tuple[str, tuple[str, ...]], ...] = (
    ("narrow", ("k1",)),
    ("wide", ("k1", "k2", "k3", "p1", "p2")),
    ("mwir", ("k1",)),
)


def camera_named(name: str) -> Camera:
    """One of the synthetic cameras, by name."""
    for known, camera in CAMERAS:
        if known == name:
            return camera
    raise CameraError(name, "is not a synthetic camera")


def terms_for(name: str) -> tuple[str, ...]:
    """The distortion terms worth fitting to a synthetic camera."""
    for known, terms in TERMS:
        if known == name:
            return terms
    raise CameraError(name, "is not a synthetic camera")


@dataclass(frozen=True)
class Board:
    """The board's dots: columns by rows, a metre across, centred on its origin."""

    columns: int = BOARD_COLUMNS
    rows: int = BOARD_ROWS
    width_m: float = BOARD_WIDTH_M


def board_points(board: Board) -> np.ndarray[tuple[int, int], np.dtype[np.float64]]:
    """The board's dots, row by row, in metres."""
    spacing = board.width_m / (board.columns - 1)
    xs = (np.arange(board.columns) - (board.columns - 1) / 2.0) * spacing
    ys = (np.arange(board.rows) - (board.rows - 1) / 2.0) * spacing
    return np.array([[x, y] for y in ys for x in xs])


def random_pose(generator: np.random.Generator, intrinsics: Intrinsics) -> Pose:
    """A board pose that fills about FILL of the image's width, tilted and a little off the axis."""
    distance = BOARD_WIDTH_M * intrinsics.fx / (FILL * intrinsics.width) * generator.uniform(1.0, 1.3)
    spin, heading = generator.uniform(0.0, 2.0 * math.pi, size=2)
    tilt = generator.uniform(LEAST_TILT_RAD, MOST_TILT_RAD)
    axis = np.array([math.cos(heading), math.sin(heading), 0.0])
    skew = np.array([[0.0, -axis[2], axis[1]], [axis[2], 0.0, -axis[0]], [-axis[1], axis[0], 0.0]])
    tilted = np.eye(3) + math.sin(tilt) * skew + (1.0 - math.cos(tilt)) * (skew @ skew)
    spun = np.array([[math.cos(spin), -math.sin(spin), 0.0], [math.sin(spin), math.cos(spin), 0.0], [0.0, 0.0, 1.0]])
    half_width = distance * intrinsics.width / (2.0 * intrinsics.fx)
    half_height = distance * intrinsics.height / (2.0 * intrinsics.fy)
    shift = generator.uniform(-OFFSET, OFFSET, size=2)
    translation = (float(shift[0] * half_width), float(shift[1] * half_height), float(distance))
    return Pose(rotation=rotation_vector(tilted @ spun), translation=translation)


def view_points(
    generator: np.random.Generator, name: str, view: View, intrinsics: Intrinsics, noise_px: float
) -> list[TargetPoint]:
    """A view's points that land on the image, with noise."""
    noisy = view.pixels + generator.normal(0.0, noise_px, size=view.pixels.shape)
    inside = (
        (view.pixels[:, 0] >= 0.0)
        & (view.pixels[:, 0] <= intrinsics.width - 1.0)
        & (view.pixels[:, 1] >= 0.0)
        & (view.pixels[:, 1] <= intrinsics.height - 1.0)
    )
    return [
        TargetPoint(name, index, float(view.board[index, 0]), float(view.board[index, 1]), *map(float, noisy[index]))
        for index in range(view.board.shape[0])
        if bool(inside[index])
    ]


def synthetic_targets(
    generator: np.random.Generator, intrinsics: Intrinsics, views: int, noise_px: float
) -> tuple[list[TargetPoint], dict[str, Pose]]:
    """Target points from views of the board, and each view's true pose."""
    board = board_points(Board())
    points: list[TargetPoint] = []
    poses: dict[str, Pose] = {}
    for index in range(views):
        name = f"v{index:02d}"
        poses[name] = random_pose(generator, intrinsics)
        exact = View(board=board, pixels=view_pixels(View(board, board), poses[name], intrinsics))
        points.extend(view_points(generator, name, exact, intrinsics, noise_px))
    return points, poses


def synthetic_sightings(generator: np.random.Generator, camera: Camera, count: int, noise_px: float) -> list[Sighting]:
    """Sightings of directions that land on random pixels, along random lines of sight."""
    sightings: list[Sighting] = []
    width, height = camera.intrinsics.width, camera.intrinsics.height
    for _ in range(count):
        line_of_sight = AltAz(generator.uniform(0.0, 2.0 * math.pi), generator.uniform(LOWEST_RAD, HIGHEST_RAD))
        u, v = generator.uniform(0.0, width - 1.0), generator.uniform(0.0, height - 1.0)
        target = sky_of_pixel(u, v, line_of_sight, camera)
        noise = generator.normal(0.0, noise_px, size=2)
        sightings.append(Sighting(line_of_sight, target, u + float(noise[0]), v + float(noise[1])))
    return sightings
