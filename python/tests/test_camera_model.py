"""The camera model and its boresight (ICS-042): projection, undistortion, and sky to pixel and back."""

import math

import numpy as np
import pytest
from hypothesis import given
from hypothesis import strategies as st

from ics_models.camera.boresight import Boresight, Camera, pixel_of_sky, sky_of_pixel
from ics_models.camera.model import (
    CameraError,
    Distortion,
    Intrinsics,
    distort,
    distortion_jacobian,
    project,
    undistort,
)
from ics_models.camera.synth import CAMERAS, camera_named
from ics_models.pointing.model import AltAz
from ics_models.tracker.geometry import CameraGeometry
from ics_models.tracker.geometry import pixel_of_sky as tracker_pixel_of_sky

ROUND_TRIP_PX: float = 1e-9
ROUND_TRIP_RAD: float = 1e-12
DERIVATIVE_STEP: float = 1e-7
NAMES: tuple[str, ...] = tuple(name for name, _ in CAMERAS)

fractions = st.floats(min_value=0.0, max_value=1.0)


def pixel_at(camera: Camera, across: float, down: float) -> tuple[float, float]:
    """The pixel at fractions of the image's width and height."""
    return across * (camera.intrinsics.width - 1.0), down * (camera.intrinsics.height - 1.0)


@pytest.mark.parametrize("name", NAMES)
@given(across=fractions, down=fractions, depth=st.floats(min_value=0.5, max_value=5000.0))
def test_undistortion_inverts_projection(name: str, across: float, down: float, depth: float) -> None:
    camera = camera_named(name)
    u, v = pixel_at(camera, across, down)
    x, y = undistort(u, v, camera.intrinsics)
    back = project((x * depth, y * depth, depth), camera.intrinsics)
    assert math.hypot(back[0] - u, back[1] - v) < ROUND_TRIP_PX


@pytest.mark.parametrize("name", NAMES)
@given(across=fractions, down=fractions)
def test_the_distortion_jacobian_is_its_derivative(name: str, across: float, down: float) -> None:
    camera = camera_named(name)
    x, y = undistort(*pixel_at(camera, across, down), camera.intrinsics)
    d = camera.intrinsics.distortion
    step = DERIVATIVE_STEP

    def at(dx: float, dy: float) -> tuple[float, float]:
        moved_x, moved_y = distort(np.array([x + dx]), np.array([y + dy]), d)
        return float(moved_x[0]), float(moved_y[0])

    by_x = [(a - b) / (2 * step) for a, b in zip(at(step, 0.0), at(-step, 0.0), strict=True)]
    by_y = [(a - b) / (2 * step) for a, b in zip(at(0.0, step), at(0.0, -step), strict=True)]
    expected = (by_x[0], by_y[0], by_x[1], by_y[1])
    assert distortion_jacobian(x, y, d) == pytest.approx(expected, abs=1e-6)


def test_a_point_behind_the_camera_is_refused() -> None:
    with pytest.raises(CameraError, match="not in front"):
        project((0.1, 0.2, -1.0), camera_named("wide").intrinsics)


def test_a_pixel_past_the_fold_does_not_undistort() -> None:
    folding = Intrinsics(100, 100, 100.0, 100.0, 50.0, 50.0, Distortion(k1=-1.0))
    with pytest.raises(CameraError, match=r"did not undistort|folds over"):
        undistort(1e4, 1e4, folding)


def test_a_fold_on_the_axis_is_refused() -> None:
    # At (0.5, 0) with p1 = 1 the Jacobian is [[1, 1], [1, 1]]: singular, exactly.
    flat = Intrinsics(100, 100, 100.0, 100.0, 50.0, 50.0, Distortion(p1=1.0))
    with pytest.raises(CameraError, match="folds over"):
        undistort(100.0, 50.0, flat)


@pytest.mark.parametrize("name", NAMES)
@given(
    across=fractions,
    down=fractions,
    azimuth=st.floats(min_value=0.0, max_value=6.28),
    elevation=st.floats(min_value=0.0, max_value=1.5),
)
def test_sky_of_pixel_inverts_pixel_of_sky(
    name: str, across: float, down: float, azimuth: float, elevation: float
) -> None:
    camera = camera_named(name)
    sight = AltAz(azimuth, elevation)
    u, v = pixel_at(camera, across, down)
    back = pixel_of_sky(sky_of_pixel(u, v, sight, camera), sight, camera)
    assert math.hypot(back[0] - u, back[1] - v) < ROUND_TRIP_PX


@pytest.mark.parametrize("mirrored", [False, True])
def test_a_plain_camera_matches_the_trackers_geometry(mirrored: bool) -> None:
    focal = 2000.0
    camera = Camera(Intrinsics(1280, 800, focal, focal, 640.0, 400.0, Distortion()), Boresight(0.0, 0.0, 0.0, mirrored))
    geometry = CameraGeometry(640.0, 400.0, 1.0 / focal, 0.0, mirrored)
    sight = AltAz(1.0, 0.4)
    for u, v in ((0.0, 0.0), (1279.0, 10.0), (300.0, 799.0)):
        direction = sky_of_pixel(u, v, sight, camera)
        assert tracker_pixel_of_sky(direction, sight, geometry) == pytest.approx((u, v), abs=1e-9)


def test_pitch_moves_a_direction_up_the_image() -> None:
    base = camera_named("narrow")
    sight = AltAz(0.5, 0.3)
    direction = sky_of_pixel(640.0, 400.0, sight, base)
    tipped = Camera(base.intrinsics, Boresight(base.boresight.yaw, base.boresight.pitch + 1e-3, 0.0, False))
    assert pixel_of_sky(direction, sight, tipped)[1] < pixel_of_sky(direction, sight, base)[1]


def test_a_direction_behind_the_camera_is_refused() -> None:
    with pytest.raises(CameraError, match="not in front"):
        pixel_of_sky(AltAz(math.pi, 0.0), AltAz(0.0, 0.0), camera_named("wide"))


def test_sky_of_pixel_gives_a_wrapped_azimuth() -> None:
    direction = sky_of_pixel(0.0, 400.0, AltAz(0.0, 0.2), camera_named("narrow"))
    assert 0.0 <= direction.azimuth < 2.0 * math.pi
    assert direction.azimuth > math.pi
