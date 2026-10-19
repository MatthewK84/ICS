"""The reference alpha-beta track and demand (ICS-038)."""

import math

import pytest

from ics_models.pointing.model import AltAz
from ics_models.tracker.centroid import TrackerError
from ics_models.tracker.demand import AxisState, demand
from ics_models.tracker.estimator import AngleEstimator, AngleState, EstimatorSettings

START_NS: int = 1_000_000_000
STEP_NS: int = 10_000_000


def test_a_steady_target_is_tracked_at_its_rate() -> None:
    track = AngleEstimator(EstimatorSettings(0.5, 0.1))
    track.start(AngleState(START_NS, 1.0, 0.5, 0.0, 0.0))
    for step in range(1, 400):
        seconds = step * STEP_NS / 1e9
        state = track.update(START_NS + step * STEP_NS, AltAz(1.0 + 0.1 * seconds, 0.5 - 0.02 * seconds))
    assert (state.azimuth_rate, state.elevation_rate) == pytest.approx((0.1, -0.02), rel=1e-6)
    ahead = state.at(state.time_ns + 500_000_000)
    assert (ahead.azimuth, ahead.elevation) == pytest.approx((state.azimuth + 0.05, state.elevation - 0.01))


def test_the_track_goes_the_short_way_across_north() -> None:
    track = AngleEstimator(EstimatorSettings(1.0, 0.0))
    track.start(AngleState(START_NS, 2.0 * math.pi - 0.001, 0.2, 0.0, 0.0))
    state = track.update(START_NS + STEP_NS, AltAz(0.001, 0.2))
    assert state.azimuth == pytest.approx(0.001)
    assert AngleState(0, 0.0, 0.0, -1.0, 0.0).at(1_000_000).azimuth == pytest.approx(2.0 * math.pi - 0.001)


def test_the_track_refuses_what_it_cannot_take() -> None:
    track = AngleEstimator(EstimatorSettings(0.5, 0.1))
    with pytest.raises(TrackerError, match="not started"):
        track.state()
    track.start(AngleState(START_NS, 1.0, 0.5, 0.0, 0.0))
    with pytest.raises(TrackerError, match="not after the last"):
        track.update(START_NS, AltAz(1.0, 0.5))


@pytest.mark.parametrize(("alpha", "beta"), [(0.0, 0.1), (1.1, 0.1), (0.5, -0.1), (0.5, 3.0)])
def test_unstable_gains_are_refused(alpha: float, beta: float) -> None:
    with pytest.raises(TrackerError, match="stable"):
        EstimatorSettings(alpha, beta)


def test_a_demand_follows_the_target_and_closes_the_error() -> None:
    mount = AxisState(START_NS, 1.0, 0.5, 0.1, 0.0)
    target = AxisState(START_NS + 100_000_000, 1.02, 0.49, 0.1, -0.01)
    azimuth_rate, elevation_rate = demand(target, mount, 2.0)
    assert azimuth_rate == pytest.approx(0.1 + 2.0 * 0.01)
    assert elevation_rate == pytest.approx(-0.01 - 2.0 * 0.01)


def test_a_demand_goes_the_short_way_and_refuses_a_negative_gain() -> None:
    mount = AxisState(START_NS, 2.0 * math.pi - 0.01, 0.5, 0.0, 0.0)
    target = AxisState(START_NS, 0.01, 0.5, 0.0, 0.0)
    assert demand(target, mount, 1.0)[0] == pytest.approx(0.02)
    with pytest.raises(TrackerError, match="push the error up"):
        demand(target, mount, -1.0)
