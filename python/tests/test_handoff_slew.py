"""The reference slew law (ICS-039)."""

import math

import pytest

from ics_models.handoff.approach import HandoffError
from ics_models.handoff.slew import SlewLimits, closing_rate, reach_seconds, slew_demand, slew_seconds
from ics_models.pointing.model import AltAz
from ics_models.tracker.demand import AxisState

LIMITS = SlewLimits(rate_rad_s=math.radians(10.0), acceleration_rad_s2=0.5, gain=6.0)
KNEE: float = 0.5 / 36.0
MOUNT_ACCELERATION: float = 1.0
LATENCY_S: float = 0.05
LOOP_S: float = 0.02
STEP_S: float = 0.001
FIELD_RAD: float = 0.0064
LATE_S: float = 0.4
OVERSHOOT_RAD: float = 1e-4


def test_a_small_error_is_closed_at_the_gain() -> None:
    assert closing_rate(0.001, LIMITS) == pytest.approx(0.006)
    assert closing_rate(-0.001, LIMITS) == pytest.approx(-0.006)
    assert closing_rate(0.0, LIMITS) == 0.0
    assert closing_rate(KNEE, LIMITS) == pytest.approx(6.0 * KNEE)


def test_a_large_error_is_closed_at_the_rate_a_steady_deceleration_stops_from() -> None:
    assert closing_rate(0.3, LIMITS) == pytest.approx(math.sqrt(2.0 * 0.5 * (0.3 - KNEE / 2.0)))
    assert closing_rate(-0.3, LIMITS) == pytest.approx(-closing_rate(0.3, LIMITS))


def test_the_two_laws_meet_with_one_slope() -> None:
    step = 1e-7
    below = (closing_rate(KNEE, LIMITS) - closing_rate(KNEE - step, LIMITS)) / step
    above = (closing_rate(KNEE + step, LIMITS) - closing_rate(KNEE, LIMITS)) / step
    assert closing_rate(KNEE + 1e-12, LIMITS) == pytest.approx(closing_rate(KNEE, LIMITS))
    assert above == pytest.approx(below, rel=1e-4)


def test_a_mount_on_its_target_is_asked_for_the_targets_rates() -> None:
    target = AxisState(1_050_000_000, 1.0 + 0.02 * 0.05, 0.5 - 0.01 * 0.05, 0.02, -0.01)
    mount = AxisState(1_000_000_000, 1.0, 0.5, 0.02, -0.01)
    assert slew_demand(target, mount, LIMITS) == pytest.approx((0.02, -0.01))


def test_a_demand_is_held_within_the_rate_limit() -> None:
    mount = AxisState(1_000_000_000, 1.0, 0.5, 0.0, 0.0)
    assert slew_demand(AxisState(1_000_000_000, 2.0, 0.1, 0.0, 0.0), mount, LIMITS) == pytest.approx(
        (LIMITS.rate_rad_s, -LIMITS.rate_rad_s)
    )
    fast = AxisState(1_000_000_000, 1.0, 0.5, 0.3, -0.3)
    assert slew_demand(fast, mount, LIMITS) == pytest.approx((LIMITS.rate_rad_s, -LIMITS.rate_rad_s))


def test_a_slew_goes_the_short_way_across_north() -> None:
    mount = AxisState(1_000_000_000, 0.005, 0.5, 0.0, 0.0)
    azimuth_rate, elevation_rate = slew_demand(
        AxisState(1_000_000_000, 2.0 * math.pi - 0.005, 0.5, 0.0, 0.0), mount, LIMITS
    )
    assert azimuth_rate == pytest.approx(-0.06)
    assert elevation_rate == 0.0


def test_the_error_is_taken_where_the_mount_will_be() -> None:
    target = AxisState(1_100_000_000, 1.0, 0.5, 0.0, 0.0)
    mount = AxisState(1_000_000_000, 1.0, 0.5, 0.01, -0.02)
    assert slew_demand(target, mount, LIMITS) == pytest.approx((-0.006, 0.012))


def slew_until_in_field(angle: float, target_rate: float) -> tuple[float, float]:
    """A mount slewed across an angle to a moving target: the seconds until it is in the field, and the overshoot."""
    azimuth, rate, command = 0.0, 0.0, 0.0
    pending: list[tuple[float, float]] = []
    arrived, overshoot = math.inf, 0.0
    for step in range(6000):
        seconds = step * STEP_S
        if step % round(LOOP_S / STEP_S) == 0:
            effect = seconds + LATENCY_S
            target = AxisState(round(effect * 1e9), angle + target_rate * effect, 0.5, target_rate, 0.0)
            pending.append(
                (effect, slew_demand(target, AxisState(round(seconds * 1e9), azimuth, 0.5, rate, 0.0), LIMITS)[0])
            )
        if pending and pending[0][0] <= seconds:
            command = pending.pop(0)[1]
        change = min(max(command - rate, -MOUNT_ACCELERATION * STEP_S), MOUNT_ACCELERATION * STEP_S)
        rate += change
        azimuth += rate * STEP_S
        error = angle + target_rate * (seconds + STEP_S) - azimuth
        arrived = min(arrived, seconds) if abs(error) < FIELD_RAD else arrived
        overshoot = max(overshoot, -error)
    return arrived, overshoot


@pytest.mark.parametrize("degrees", [2.0, 15.0, 40.0])
def test_a_mount_twice_as_quick_as_planned_arrives_on_time_without_overshoot(degrees: float) -> None:
    angle = math.radians(degrees)
    arrived, overshoot = slew_until_in_field(angle, 0.02)
    assert arrived < slew_seconds(angle, LIMITS) + LATE_S
    assert overshoot < OVERSHOOT_RAD


def test_a_short_slew_never_reaches_the_rate_limit() -> None:
    assert slew_seconds(0.02, LIMITS) == pytest.approx(2.0 * math.sqrt(0.02 / 0.5))
    assert slew_seconds(-0.02, LIMITS) == slew_seconds(0.02, LIMITS)
    assert slew_seconds(0.0, LIMITS) == 0.0


def test_a_long_slew_cruises_at_the_rate_limit() -> None:
    rate = LIMITS.rate_rad_s
    assert slew_seconds(1.0, LIMITS) == pytest.approx(1.0 / rate + rate / 0.5)
    turning = rate * rate / 0.5
    assert slew_seconds(turning, LIMITS) == pytest.approx(turning / rate + rate / 0.5)


def test_a_mount_reaches_a_direction_when_its_slower_axis_does() -> None:
    first = AltAz(0.1, 0.3)
    assert reach_seconds(first, AltAz(0.6, 0.35), LIMITS) == pytest.approx(slew_seconds(0.5, LIMITS))
    assert reach_seconds(first, AltAz(0.12, 0.9), LIMITS) == pytest.approx(slew_seconds(0.6, LIMITS))
    assert reach_seconds(first, AltAz(2.0 * math.pi - 0.1, 0.3), LIMITS) == pytest.approx(slew_seconds(0.2, LIMITS))


@pytest.mark.parametrize("values", [(0.0, 0.5, 6.0), (0.2, -0.5, 6.0), (0.2, 0.5, 0.0), (math.inf, 0.5, 6.0)])
def test_limits_that_are_not_positive_are_refused(values: tuple[float, float, float]) -> None:
    with pytest.raises(HandoffError, match="cannot be made"):
        SlewLimits(*values)
