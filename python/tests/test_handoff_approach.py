"""The reference closest approach (ICS-039)."""

import numpy as np
import pytest

from ics_models.cue.kinematics import STATE_SIZE, Estimate
from ics_models.handoff.approach import Approach, ApproachSettings, HandoffError, closest_approach

NOW_NS: int = 1_000_000_000
SETTINGS = ApproachSettings(launch_speed_m_s=3.0, horizon_ns=60_000_000_000)


def estimate(
    position: tuple[float, float, float], velocity: tuple[float, float, float], time_ns: int = NOW_NS
) -> Estimate:
    return Estimate(time_ns, np.array([*position, *velocity, 0.0, 0.0, 0.0]), np.eye(STATE_SIZE))


def test_a_head_on_interceptor_meets_its_target_when_the_gap_is_closed() -> None:
    target = estimate((0.0, 0.0, 30.0), (5.0, 0.0, 0.0))
    interceptor = estimate((100.0, 0.0, 30.0), (-15.0, 0.0, 0.0))
    assert closest_approach(target, interceptor, SETTINGS) == Approach(NOW_NS + 5_000_000_000, 0.0)


def test_the_miss_distance_is_the_offset_across_the_closing_velocity() -> None:
    target = estimate((0.0, 0.0, 30.0), (0.0, 0.0, 0.0))
    interceptor = estimate((80.0, 3.0, 34.0), (-20.0, 0.0, 0.0))
    found = closest_approach(target, interceptor, SETTINGS)
    assert found is not None
    assert found.time_ns == NOW_NS + 4_000_000_000
    assert found.miss_m == pytest.approx(5.0)


def test_the_time_is_rounded_to_the_nearest_nanosecond() -> None:
    target = estimate((0.0, 0.0, 0.0), (0.0, 0.0, 0.0))
    interceptor = estimate((1.0, 0.0, 0.0), (-3.0, 0.0, 0.0))
    found = closest_approach(target, interceptor, SETTINGS)
    assert found is not None
    assert found.time_ns == NOW_NS + 333_333_333


def test_an_interceptor_that_has_not_launched_has_no_approach() -> None:
    target = estimate((0.0, 0.0, 30.0), (10.0, 0.0, 0.0))
    parked = estimate((100.0, 0.0, 0.0), (0.0, 2.9, 0.0))
    assert closest_approach(target, parked, SETTINGS) is None
    assert closest_approach(target, estimate((100.0, 0.0, 0.0), (0.0, 3.0, 0.0)), SETTINGS) is not None


def test_vehicles_that_are_not_closing_have_no_approach() -> None:
    target = estimate((0.0, 0.0, 30.0), (5.0, 0.0, 0.0))
    assert closest_approach(target, estimate((100.0, 0.0, 30.0), (15.0, 0.0, 0.0)), SETTINGS) is None
    assert closest_approach(target, estimate((100.0, 0.0, 30.0), (5.0, 0.0, 0.0)), SETTINGS) is None
    assert closest_approach(target, estimate((0.0, 100.0, 30.0), (9.0, 0.0, 0.0)), SETTINGS) is None


def test_an_approach_past_the_horizon_is_not_predicted() -> None:
    target = estimate((0.0, 0.0, 30.0), (0.0, 0.0, 0.0))
    assert closest_approach(target, estimate((239.9, 0.0, 30.0), (-4.0, 0.0, 0.0)), SETTINGS) is not None
    assert closest_approach(target, estimate((240.0, 0.0, 30.0), (-4.0, 0.0, 0.0)), SETTINGS) is None


def test_the_estimates_must_be_at_one_time() -> None:
    target = estimate((0.0, 0.0, 30.0), (5.0, 0.0, 0.0))
    late = estimate((100.0, 0.0, 30.0), (-15.0, 0.0, 0.0), NOW_NS + 1)
    with pytest.raises(HandoffError, match="not one time"):
        closest_approach(target, late, SETTINGS)


@pytest.mark.parametrize(("speed", "horizon_ns"), [(0.0, 1), (-1.0, 1), (float("nan"), 1), (3.0, 0)])
def test_settings_that_cannot_be_used_are_refused(speed: float, horizon_ns: int) -> None:
    with pytest.raises(HandoffError, match="cannot be used"):
        ApproachSettings(speed, horizon_ns)
