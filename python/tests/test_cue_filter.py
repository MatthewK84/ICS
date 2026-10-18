"""The cue filter: prediction, exact truths, late reports, the gate and the coast limit (ICS-037)."""

import numpy as np
import pytest

from ics_models.cue.filter import CueError, CueFilter, FilterSettings, Measurement, Outcome
from ics_models.cue.kinematics import STATE_SIZE, Estimate, ProcessNoise, predict, process_noise, transition

NOISE = ProcessNoise(horizontal=4.0, vertical=1.0)
SETTINGS = FilterSettings(
    noise=NOISE,
    gate=16.27,
    history_ns=2_000_000_000,
    coast_limit_ns=2_000_000_000,
    capacity=64,
    initial_velocity_sigma_m_s=20.0,
    initial_acceleration_sigma_m_s2=10.0,
)
PERIOD_NS: int = 100_000_000
REPORTS: int = 60
EXACT_M: float = 1e-6
SAME: float = 1e-12
CLOSE_M: float = 0.05


def report(time_ns: int, position: np.ndarray, velocity: np.ndarray | None = None, sigma: float = 0.5) -> Measurement:
    """A report at a time, with the same sigma on every axis."""
    moving = None if velocity is None else (float(velocity[0]), float(velocity[1]), float(velocity[2]))
    return Measurement(time_ns, (float(position[0]), float(position[1]), float(position[2])), sigma, sigma, moving, 0.2)


def truth(time_s: float) -> tuple[np.ndarray, np.ndarray]:
    """A constant-acceleration target's position and velocity."""
    start = np.array([-200.0, 10.0, 35.0])
    velocity = np.array([5.0, -1.0, 0.5])
    acceleration = np.array([0.8, 0.4, -0.1])
    return start + velocity * time_s + acceleration * time_s * time_s / 2.0, velocity + acceleration * time_s


def flown(count: int, rng: np.random.Generator | None = None) -> list[Measurement]:
    """Reports of the constant-acceleration target every 100 ms, noisy when rng is given."""
    reports: list[Measurement] = []
    for number in range(count):
        position, velocity = truth(number * PERIOD_NS / 1e9)
        if rng is not None:
            position = position + rng.normal(0.0, 0.5, 3)
        reports.append(report(number * PERIOD_NS, position, velocity))
    return reports


def test_prediction_composes_exactly() -> None:
    start = Estimate(0, np.arange(STATE_SIZE, dtype=float), np.eye(STATE_SIZE))
    once = predict(start, 700_000_000, NOISE)
    twice = predict(predict(start, 300_000_000, NOISE), 700_000_000, NOISE)
    np.testing.assert_allclose(twice.state, once.state, rtol=SAME)
    np.testing.assert_allclose(twice.covariance, once.covariance, rtol=1e-11, atol=1e-12)


def test_prediction_refuses_to_go_back() -> None:
    with pytest.raises(ValueError, match="cannot predict back"):
        predict(Estimate(10, np.zeros(STATE_SIZE), np.eye(STATE_SIZE)), 9, NOISE)


def test_the_noise_is_symmetric_and_grows() -> None:
    small, large = process_noise(0.1, NOISE), process_noise(1.0, NOISE)
    np.testing.assert_allclose(small, small.T)
    assert np.all(np.diag(large) > np.diag(small))
    assert transition(0.0) == pytest.approx(np.eye(STATE_SIZE))


def test_follows_a_constant_acceleration_exactly() -> None:
    cue_filter = CueFilter(SETTINGS)
    outcomes = {cue_filter.add(measured) for measured in flown(REPORTS)}
    assert outcomes == {Outcome.TAKEN}
    ahead_ns = (REPORTS - 1) * PERIOD_NS + 300_000_000
    estimate = cue_filter.estimate_at(ahead_ns)
    position, velocity = truth(ahead_ns / 1e9)
    np.testing.assert_allclose(estimate.state[0:3], position, atol=EXACT_M * 1e3)
    np.testing.assert_allclose(estimate.state[3:6], velocity, atol=EXACT_M * 1e3)


def test_late_reports_give_the_in_order_result() -> None:
    reports = flown(REPORTS, np.random.default_rng(37))
    in_order = CueFilter(SETTINGS)
    for measured in reports:
        in_order.add(measured)
    shuffled = CueFilter(SETTINGS)
    arrival = reports[:]
    # Move every fifth report three places later, as a slower source would deliver it.
    for number in range(4, REPORTS - 3, 5):
        arrival[number], arrival[number + 3] = arrival[number + 3], arrival[number]
    outcomes = [shuffled.add(measured) for measured in arrival]
    assert Outcome.LATE in outcomes
    final_ns = reports[-1].time_ns
    np.testing.assert_allclose(shuffled.estimate_at(final_ns).state, in_order.estimate_at(final_ns).state, rtol=SAME)


def test_a_report_before_the_first_refilters_from_it() -> None:
    reports = flown(3)
    cue_filter = CueFilter(SETTINGS)
    assert cue_filter.add(reports[1]) == Outcome.TAKEN
    assert cue_filter.add(reports[0]) == Outcome.LATE
    assert cue_filter.add(reports[2]) == Outcome.TAKEN
    straight = CueFilter(SETTINGS)
    for measured in reports:
        straight.add(measured)
    np.testing.assert_allclose(
        cue_filter.estimate_at(reports[2].time_ns).state, straight.estimate_at(reports[2].time_ns).state
    )


def test_gates_an_outlier_and_keeps_going() -> None:
    reports = flown(20, np.random.default_rng(5))
    cue_filter = CueFilter(SETTINGS)
    for measured in reports[:10]:
        cue_filter.add(measured)
    position, velocity = truth(reports[10].time_ns / 1e9)
    assert cue_filter.add(report(reports[10].time_ns, position + 40.0, velocity)) == Outcome.GATED
    for measured in reports[11:]:
        assert cue_filter.add(measured) == Outcome.TAKEN
    final, _ = truth(reports[-1].time_ns / 1e9)
    np.testing.assert_allclose(cue_filter.estimate_at(reports[-1].time_ns).state[0:3], final, atol=CLOSE_M * 10)


def test_refuses_reports_older_than_the_history() -> None:
    reports = flown(40)
    cue_filter = CueFilter(SETTINGS)
    for measured in reports[10:]:
        cue_filter.add(measured)
    assert cue_filter.add(reports[0]) == Outcome.TOO_OLD
    assert cue_filter.add(reports[10]) == Outcome.TOO_OLD


def test_estimates_only_within_the_history_and_the_coast_limit() -> None:
    cue_filter = CueFilter(SETTINGS)
    with pytest.raises(CueError, match="no report"):
        cue_filter.estimate_at(0)
    for measured in flown(40):
        cue_filter.add(measured)
    last_ns = 39 * PERIOD_NS
    assert cue_filter.estimate_at(last_ns + SETTINGS.coast_limit_ns).time_ns == last_ns + SETTINGS.coast_limit_ns
    with pytest.raises(CueError, match="coast limit"):
        cue_filter.estimate_at(last_ns + SETTINGS.coast_limit_ns + 1)
    # The base, the checkpoint of the last report dropped, still answers between it and the history.
    held_from_ns = last_ns - SETTINGS.history_ns
    assert cue_filter.estimate_at(held_from_ns - 1).time_ns == held_from_ns - 1
    with pytest.raises(CueError, match="no report"):
        cue_filter.estimate_at(0)


def test_capacity_bounds_the_history() -> None:
    small = FilterSettings(**{**SETTINGS.__dict__, "capacity": 4})
    cue_filter = CueFilter(small)
    for measured in flown(10):
        cue_filter.add(measured)
    # The fifth report was dropped for room, so its checkpoint is the base: an earlier report is too old.
    assert cue_filter.add(flown(6)[4]) == Outcome.TOO_OLD
    assert cue_filter.add(flown(6)[5]) == Outcome.LATE


def test_position_only_reports_start_still() -> None:
    cue_filter = CueFilter(SETTINGS)
    cue_filter.add(report(0, np.array([1.0, 2.0, 3.0])))
    estimate = cue_filter.estimate_at(0)
    np.testing.assert_allclose(estimate.state, [1.0, 2.0, 3.0, 0, 0, 0, 0, 0, 0])
    assert estimate.covariance[3, 3] == pytest.approx(SETTINGS.initial_velocity_sigma_m_s**2)
    moved_to_m = 1.5
    assert cue_filter.add(report(PERIOD_NS, np.array([moved_to_m, 2.0, 3.0]))) == Outcome.TAKEN
    moved = cue_filter.estimate_at(PERIOD_NS)
    assert 1.0 < moved.state[0] < moved_to_m
    assert moved.state[3] > 0.0
