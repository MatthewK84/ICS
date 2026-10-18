"""The cue's geometry and pointing: ENU positions, station axes, angles, rates and sigma (ICS-037)."""

import csv
import math
from pathlib import Path

import numpy as np
import pytest

from ics_models.cue.filter import CueError
from ics_models.cue.geometry import Geodetic, axes_between, to_enu
from ics_models.cue.kinematics import STATE_SIZE, Estimate, ProcessNoise, predict
from ics_models.cue.pointing import cue_for, station_at, wrap_azimuth

GOLDEN_FRAMES = Path(__file__).resolve().parents[2] / "golden" / "frames"
ORIGIN = Geodetic(40.0, -100.0, 700.0)
# golden/frames prints positions to the nanometre, far from the origin.
FRAMES_M: float = 2e-6
SAME_RAD: float = 1e-12
STEP_NS: int = 1_000_000
RATE_RAD_S: float = 1e-7


def estimate_at(position: list[float], velocity: list[float], sigma: float = 1.0) -> Estimate:
    """An estimate with a position, a velocity and an isotropic position sigma."""
    state = np.zeros(STATE_SIZE)
    state[0:3] = position
    state[3:6] = velocity
    covariance = np.eye(STATE_SIZE)
    covariance[0:3, 0:3] *= sigma * sigma
    return Estimate(0, state, covariance)


def test_positions_match_the_frames_golden_vectors() -> None:
    with (GOLDEN_FRAMES / "geodetic-enu.csv").open(newline="") as file:
        rows = list(csv.DictReader(file))
    assert rows
    for row in rows:
        origin = Geodetic(
            float(row["origin_latitude_deg"]),
            float(row["origin_longitude_deg"]),
            float(row["origin_height_ellipsoid_m"]),
        )
        point = Geodetic(float(row["latitude_deg"]), float(row["longitude_deg"]), float(row["height_ellipsoid_m"]))
        expected = [float(row["east_m"]), float(row["north_m"]), float(row["up_m"])]
        np.testing.assert_allclose(to_enu(origin, point), expected, atol=FRAMES_M, err_msg=row["id"])


def test_station_axes_turn_with_the_normal() -> None:
    at_origin = station_at(ORIGIN, ORIGIN)
    np.testing.assert_allclose(at_origin.position, np.zeros(3), atol=1e-9)
    np.testing.assert_allclose(at_origin.axes, np.eye(3), atol=1e-15)
    east = Geodetic(40.0, -99.9965, 702.0)
    axes = axes_between(ORIGIN, east)
    np.testing.assert_allclose(axes @ axes.T, np.eye(3), atol=1e-15)
    tilt = math.acos(axes[2, 2])
    assert tilt == pytest.approx(298.0 / 6.38e6, rel=0.01)


@pytest.mark.parametrize(
    ("position", "azimuth", "elevation"),
    [
        ([0.0, 100.0, 0.0], 0.0, 0.0),
        ([100.0, 0.0, 100.0], math.pi / 2.0, math.pi / 4.0),
        ([0.0, -100.0, -10.0], math.pi, math.atan2(-10.0, 100.0)),
        ([-100.0, 0.0, 0.0], 3.0 * math.pi / 2.0, 0.0),
    ],
)
def test_azimuth_runs_clockwise_from_north(position: list[float], azimuth: float, elevation: float) -> None:
    cue = cue_for(estimate_at(position, [0.0, 0.0, 0.0]), station_at(ORIGIN, ORIGIN))
    # Due north is 0 or just under 2π, so compare the angles around the circle.
    assert math.remainder(cue.azimuth_rad - azimuth, 2.0 * math.pi) == pytest.approx(0.0, abs=SAME_RAD)
    assert 0.0 <= cue.azimuth_rad < 2.0 * math.pi
    assert cue.elevation_rad == pytest.approx(elevation, abs=SAME_RAD)
    assert cue.range_m == pytest.approx(float(np.linalg.norm(position)))


def test_rates_are_the_angles_derivatives() -> None:
    station = station_at(ORIGIN, Geodetic(39.998, -100.0015, 702.0))
    still = ProcessNoise(0.0, 0.0)
    now = estimate_at([30.0, -40.0, 35.0], [5.0, -2.0, 1.5])
    before = predict(now, STEP_NS, still)
    after = predict(now, 2 * STEP_NS, still)
    cue, early, late = cue_for(before, station), cue_for(now, station), cue_for(after, station)
    seconds = 2 * STEP_NS / 1e9
    assert cue.azimuth_rate_rad_s == pytest.approx((late.azimuth_rad - early.azimuth_rad) / seconds, abs=RATE_RAD_S)
    assert cue.elevation_rate_rad_s == pytest.approx(
        (late.elevation_rad - early.elevation_rad) / seconds, abs=RATE_RAD_S
    )


def test_sigma_is_the_transverse_error_over_range() -> None:
    station = station_at(ORIGIN, ORIGIN)
    cue = cue_for(estimate_at([200.0, 150.0, 30.0], [0.0, 0.0, 0.0], sigma=2.0), station)
    assert cue.sigma_rad == pytest.approx(2.0 / cue.range_m)
    stretched = estimate_at([0.0, 250.0, 0.0], [0.0, 0.0, 0.0])
    stretched.covariance[0, 0] = 9.0  # East is across the line of sight here; north is along it.
    stretched.covariance[1, 1] = 100.0
    assert cue_for(stretched, station).sigma_rad == pytest.approx(3.0 / 250.0)


def test_wraps_a_tiny_negative_angle_to_north() -> None:
    assert wrap_azimuth(-1e-17) == 0.0
    assert wrap_azimuth(-math.pi / 2.0) == pytest.approx(3.0 * math.pi / 2.0)
    assert wrap_azimuth(1.0) == 1.0


@pytest.mark.parametrize("position", [[0.0, 0.0, 50.0], [0.0, 0.0, 0.5]])
def test_refuses_a_target_overhead_or_at_the_station(position: list[float]) -> None:
    with pytest.raises(CueError, match="no direction"):
        cue_for(estimate_at(position, [0.0, 0.0, 0.0]), station_at(ORIGIN, ORIGIN))
