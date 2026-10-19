"""The reference camera geometry (ICS-038)."""

import math

import pytest

from ics_models.pointing.model import AltAz
from ics_models.tracker.centroid import TrackerError
from ics_models.tracker.geometry import (
    CameraGeometry,
    pixel_of_sky,
    pixel_of_tangent,
    sky_of_pixel,
    sky_of_tangent,
    tangent_of_pixel,
    tangent_of_sky,
    wrap_azimuth,
)

CAMERA = CameraGeometry(32.0, 24.0, 1e-5, 0.0, False)
SIGHT = AltAz(1.0, 0.5)
WEST_OF_NORTH: float = 2.0 * math.pi - 0.011


def test_the_boresight_looks_along_the_line_of_sight() -> None:
    sky = sky_of_pixel(32.0, 24.0, SIGHT, CAMERA)
    assert (sky.azimuth, sky.elevation) == pytest.approx((1.0, 0.5), abs=1e-15)


def test_right_is_azimuth_and_up_is_elevation() -> None:
    assert tangent_of_pixel(42.0, 24.0, CAMERA) == pytest.approx((1e-4, 0.0))
    assert tangent_of_pixel(32.0, 14.0, CAMERA) == pytest.approx((0.0, 1e-4))
    right = sky_of_pixel(42.0, 24.0, SIGHT, CAMERA)
    assert (right.azimuth - 1.0) * math.cos(0.5) == pytest.approx(1e-4, rel=1e-6)
    assert sky_of_pixel(32.0, 14.0, SIGHT, CAMERA).elevation - 0.5 == pytest.approx(1e-4, rel=1e-6)


def test_roll_and_mirroring_turn_the_image() -> None:
    rolled = CameraGeometry(32.0, 24.0, 1e-5, math.pi / 2.0, False)
    assert tangent_of_pixel(42.0, 24.0, rolled) == pytest.approx((0.0, 1e-4))
    mirrored = CameraGeometry(32.0, 24.0, 1e-5, 0.0, True)
    assert tangent_of_pixel(42.0, 24.0, mirrored) == pytest.approx((-1e-4, 0.0))


@pytest.mark.parametrize("camera", [CAMERA, CameraGeometry(30.5, 20.25, 2e-5, -2.1, True)])
def test_pixels_and_directions_round_trip(camera: CameraGeometry) -> None:
    for x, y in ((0.0, 0.0), (63.0, 47.0), (12.25, 40.5)):
        assert pixel_of_tangent(*tangent_of_pixel(x, y, camera), camera) == pytest.approx((x, y))
        for sight in (SIGHT, AltAz(6.28, 1.35), AltAz(0.0, 0.0)):
            back = pixel_of_sky(sky_of_pixel(x, y, sight, camera), sight, camera)
            assert back == pytest.approx((x, y), abs=1e-6)


def test_azimuth_wraps_across_north() -> None:
    sky = sky_of_tangent(-0.01, 0.0, AltAz(0.0, 0.2))
    assert WEST_OF_NORTH < sky.azimuth < 2.0 * math.pi
    assert tangent_of_sky(sky, AltAz(0.0, 0.2)) == pytest.approx((-0.01, 0.0))


def test_azimuths_wrap_into_one_turn() -> None:
    assert wrap_azimuth(-1e-17) == 0.0
    assert wrap_azimuth(2.0 * math.pi + 0.5) == pytest.approx(0.5)
    assert wrap_azimuth(-0.5) == pytest.approx(2.0 * math.pi - 0.5)


def test_a_direction_behind_the_camera_has_no_pixel() -> None:
    with pytest.raises(TrackerError, match="90 degrees or more"):
        tangent_of_sky(AltAz(1.0 + math.pi, 0.0), AltAz(1.0, 0.0))
