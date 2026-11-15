"""The reference refined closest approach (ICS-040)."""

import numpy as np
import pytest

from ics_models.cue.kinematics import STATE_SIZE, Estimate
from ics_models.handoff.approach import Approach, ApproachSettings, closest_approach
from ics_models.trigger.approach import refined_approach

NOW_NS: int = 1_000_000_000
SETTINGS = ApproachSettings(launch_speed_m_s=3.0, horizon_ns=60_000_000_000)
Triple = tuple[float, float, float]
STILL: Triple = (0.0, 0.0, 0.0)


def estimate(position: Triple, velocity: Triple, acceleration: Triple = STILL) -> Estimate:
    return Estimate(NOW_NS, np.array([*position, *velocity, *acceleration]), np.eye(STATE_SIZE))


def test_vehicles_that_keep_their_velocity_meet_where_the_coarse_approach_says() -> None:
    target = estimate((0.0, 0.0, 30.0), (5.0, 0.0, 0.0))
    interceptor = estimate((100.0, 3.0, 34.0), (-15.0, 0.0, 0.0))
    assert refined_approach(target, interceptor, SETTINGS) == closest_approach(target, interceptor, SETTINGS)


def test_an_interceptor_that_is_speeding_up_arrives_sooner() -> None:
    target = estimate((0.0, 0.0, 30.0), STILL)
    interceptor = estimate((100.0, 0.0, 30.0), (-10.0, 0.0, 0.0), (-10.0, 0.0, 0.0))
    found = refined_approach(target, interceptor, SETTINGS)
    assert found is not None
    seconds = (found.time_ns - NOW_NS) / 1e9
    assert seconds == pytest.approx(-1.0 + 21.0**0.5, abs=1e-9)
    assert found != closest_approach(target, interceptor, SETTINGS)
    assert found.miss_m == pytest.approx(0.0, abs=1e-6)


def test_a_target_that_turns_away_is_missed_by_what_the_turn_opens() -> None:
    target = estimate((0.0, 0.0, 30.0), STILL, (0.0, 2.0, 0.0))
    interceptor = estimate((100.0, 0.0, 30.0), (-20.0, 0.0, 0.0))
    found = refined_approach(target, interceptor, SETTINGS)
    coarse = closest_approach(target, interceptor, SETTINGS)
    assert found is not None
    assert coarse is not None
    assert found.time_ns < coarse.time_ns
    assert found.miss_m == pytest.approx(22.57, abs=0.01)


def test_no_coarse_approach_means_no_refined_one() -> None:
    target = estimate((0.0, 0.0, 30.0), (5.0, 0.0, 0.0))
    assert refined_approach(target, estimate((100.0, 0.0, 30.0), (15.0, 0.0, 0.0)), SETTINGS) is None


def test_steps_that_find_no_minimum_leave_the_coarse_approach() -> None:
    target = estimate((0.0, 0.0, 30.0), STILL)
    swinging = estimate((100.0, 500.0, 30.0), (-4.0, 0.0, 0.0), (0.0, -0.2, 0.0))
    assert refined_approach(target, swinging, SETTINGS) == Approach(NOW_NS + 25_000_000_000, 500.0)


def test_steps_that_leave_the_horizon_leave_the_coarse_approach() -> None:
    target = estimate((0.0, 0.0, 30.0), STILL)
    slowing = estimate((150.0, 0.0, 30.0), (-4.0, 0.0, 0.0), (0.051, 0.0, 0.0))
    assert refined_approach(target, slowing, SETTINGS) == Approach(NOW_NS + 37_500_000_000, 0.0)
