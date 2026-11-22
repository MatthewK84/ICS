"""Closest approach, miss distance and contact time (ICS-046).

The done-when criterion: on synthetic truth the miss distance is within 1 mm.
"""

import math

import numpy as np
import pytest

from ics_models.cue.kinematics import STATE_SIZE, Estimate, Vector
from ics_models.endgame.approach import EncounterSettings, encounter
from ics_models.endgame.motion import EndgameError
from ics_models.endgame.truth import Leg, flight

NOW_NS: int = 2_000_000_000
SETTINGS = EncounterSettings(horizon_ns=5_000_000_000, contact_radius_m=0.5)
STILL = np.zeros(STATE_SIZE)
MILLIMETRE: float = 1e-3
MICROMETRE: float = 1e-6
NEAR_TIME_NS: int = 10_000
FAR_TIME_NS: int = 1_000


def estimate(state: Vector, sigma: float = 0.0, time_ns: int = NOW_NS) -> Estimate:
    return Estimate(time_ns, np.asarray(state, dtype=np.float64), np.eye(STATE_SIZE) * sigma * sigma)


def truth_closest(target: tuple[Vector, Leg], interceptor: tuple[Vector, Leg]) -> tuple[int, float]:
    """The truth's closest approach in its first 4 s: sampled every 1 ms, then every 1 µs around the least."""
    found_ns = 0
    for step_ns, around_ns in ((1_000_000, 2_000_000_000), (1_000, 2_000_000)):
        times = [found_ns + offset for offset in range(-around_ns, around_ns + 1, step_ns) if found_ns + offset >= 0]
        if found_ns == 0:
            times = list(range(0, 2 * around_ns + 1, step_ns))
        firsts = flight(target[0], 0, (target[1],), times)
        seconds = flight(interceptor[0], 0, (interceptor[1],), times)
        gaps = [float(np.linalg.norm(b[0:3] - a[0:3])) for a, b in zip(firsts, seconds, strict=True)]
        found_ns, miss = times[int(np.argmin(gaps))], min(gaps)
    return found_ns, miss


def passing(target: tuple[Vector, Leg], leg: Leg) -> tuple[Vector, Leg]:
    """An interceptor on leg that would pass 25 cm above the target at 2 s if both kept their acceleration."""
    meet = flight(target[0], 0, (target[1],), [2_000_000_000])[0]
    velocity, acceleration = np.array([20.0, 15.0, 3.0]), np.array(leg.acceleration)
    position = meet[0:3] + np.array([0.0, 0.0, 0.25]) - 2.0 * velocity - 2.0 * acceleration
    return np.concatenate([position, velocity, acceleration]), leg


LEGS: tuple[tuple[Leg, Leg], ...] = (
    (Leg(4_000_000_000), Leg(4_000_000_000)),
    (Leg(4_000_000_000, acceleration=(1.0, 0.5, -2.0)), Leg(4_000_000_000, acceleration=(-3.0, 2.0, 4.0))),
    (Leg(4_000_000_000, turn_rate_rad_s=0.5), Leg(4_000_000_000, acceleration=(2.0, -1.0, -1.0))),
    (Leg(4_000_000_000, turn_rate_rad_s=-1.0), Leg(4_000_000_000, turn_rate_rad_s=0.8)),
)


def found_against_truth(target_leg: Leg, interceptor_leg: Leg, lead_ns: int) -> tuple[float, int]:
    """The solver's miss and time errors against the truth, from estimates lead_ns before the true approach."""
    target = (np.array([0.0, 0.0, 100.0, 15.0, 5.0, 0.0, 0.0, 0.0, 0.0]), target_leg)
    interceptor = passing(target, interceptor_leg)
    true_ns, true_miss = truth_closest(target, interceptor)
    start_ns = max(0, true_ns - lead_ns)
    first = flight(target[0], 0, (target_leg,), [start_ns])[0]
    second = flight(interceptor[0], 0, (interceptor_leg,), [start_ns])[0]
    found = encounter(estimate(first, time_ns=start_ns), estimate(second, time_ns=start_ns), SETTINGS)
    assert found is not None
    return abs(found.miss_m - true_miss), abs(found.time_ns - true_ns)


@pytest.mark.parametrize(("target_leg", "interceptor_leg"), LEGS)
def test_the_miss_on_synthetic_truth_is_within_a_millimetre(target_leg: Leg, interceptor_leg: Leg) -> None:
    miss_error, time_error = found_against_truth(target_leg, interceptor_leg, 20_000_000)
    assert miss_error <= MILLIMETRE
    assert time_error <= NEAR_TIME_NS


@pytest.mark.parametrize(("target_leg", "interceptor_leg"), LEGS[0:2])
def test_without_turns_the_miss_is_exact_from_far_out(target_leg: Leg, interceptor_leg: Leg) -> None:
    miss_error, time_error = found_against_truth(target_leg, interceptor_leg, 4_000_000_000)
    assert miss_error <= MICROMETRE
    assert time_error <= FAR_TIME_NS


def state(
    position: tuple[float, float, float],
    velocity: tuple[float, float, float],
    acceleration: tuple[float, float, float] = (0.0, 0.0, 0.0),
) -> Vector:
    return np.array([*position, *velocity, *acceleration])


@pytest.mark.parametrize(
    "interceptor",
    [
        state((5.0, 0.0, 0.0), (10.0, 0.0, 0.0)),
        state((5.0, 0.0, 0.0), (0.0, 0.0, 0.0)),
        state((-500.0, 1.0, 0.0), (10.0, 0.0, 0.0)),
    ],
    ids=["receding", "parked", "past the horizon"],
)
def test_there_is_no_encounter_to_report(interceptor: Vector) -> None:
    assert encounter(estimate(STILL), estimate(interceptor), SETTINGS) is None


def test_a_head_on_pass_has_its_time_contact_and_sigma() -> None:
    found = encounter(estimate(STILL, 0.1), estimate(state((-20.0, 0.3, 0.0), (20.0, 0.0, 0.0)), 0.2), SETTINGS)
    assert found is not None
    assert found.time_ns == NOW_NS + 1_000_000_000
    assert found.miss_m == pytest.approx(0.3, abs=1e-12)
    # Across the line of flight only position and velocity count: σ² = (0.1² + 0.2²)(1 + 1² + 1/4).
    assert found.miss_sigma_m == pytest.approx(math.sqrt(0.05 * 2.25), rel=1e-12)
    assert found.contact_ns == NOW_NS + 1_000_000_000 - math.floor(math.sqrt(0.25 - 0.09) / 20.0 * 1e9 + 0.5)


def test_a_direct_hit_spreads_sigma_over_the_axes() -> None:
    found = encounter(estimate(STILL, 0.1), estimate(state((-20.0, 0.0, 0.0), (20.0, 0.0, 0.0))), SETTINGS)
    assert found is not None
    assert found.miss_m == 0.0
    assert found.miss_sigma_m == pytest.approx(0.1 * math.sqrt(2.25), rel=1e-12)


def test_bodies_already_in_contact_touch_now() -> None:
    found = encounter(estimate(STILL), estimate(state((0.2, 0.1, 0.0), (-30.0, 1.0, 0.0))), SETTINGS)
    assert found is not None
    assert found.contact_ns == NOW_NS


def test_an_interceptor_that_turns_back_meets_on_its_way_in() -> None:
    interceptor = state((5.0, 0.2, 0.0), (10.0, 0.0, 0.0), (-20.0, 0.0, 0.0))
    found = encounter(estimate(STILL), estimate(interceptor), SETTINGS)
    assert found is not None
    assert found.miss_m == pytest.approx(0.2, abs=1e-12)
    assert found.time_ns == NOW_NS + math.floor((10.0 + math.sqrt(300.0)) / 20.0 * 1e9 + 0.5)
    inside = (10.0 + math.sqrt(100.0 + 40.0 * (5.0 - math.sqrt(0.21)))) / 20.0
    assert found.contact_ns == NOW_NS + math.floor(inside * 1e9 + 0.5)


def test_one_that_falls_short_is_closest_where_it_stops() -> None:
    found = encounter(
        estimate(STILL), estimate(state((-20.0, 1.0, 0.0), (20.0, 0.0, 0.0), (-15.0, 0.0, 0.0))), SETTINGS
    )
    assert found is not None
    assert found.time_ns == NOW_NS + 1_333_333_333
    assert found.contact_ns is None


def test_estimates_at_different_times_are_refused() -> None:
    with pytest.raises(EndgameError):
        encounter(estimate(STILL), estimate(STILL, time_ns=NOW_NS + 1), SETTINGS)


@pytest.mark.parametrize(("horizon", "radius"), [(0, 1.0), (1, -1.0), (1, math.nan)])
def test_unusable_settings_are_refused(horizon: int, radius: float) -> None:
    with pytest.raises(EndgameError):
        EncounterSettings(horizon, radius)
