"""The IMM's motion models and the synthetic truth (ICS-046)."""

import numpy as np
import pytest

from ics_models.cue.kinematics import STATE_SIZE
from ics_models.endgame.motion import EndgameError, Kind, MotionModel, process_noise, transition
from ics_models.endgame.truth import Leg, flight

CV = MotionModel(Kind.CONSTANT_VELOCITY, 0.5, 0.2)
CA = MotionModel(Kind.CONSTANT_ACCELERATION, 10.0, 4.0)
LEFT = MotionModel(Kind.TURN, 0.5, 0.2, 0.5)
ROUNDING: float = 1e-18
START = np.array([1.0, -2.0, 50.0, 20.0, 5.0, -3.0, 1.0, 2.0, -1.0])


def test_constant_velocity_moves_with_velocity_and_drops_acceleration() -> None:
    moved = transition(CV, 0.5) @ START
    np.testing.assert_allclose(moved, [11.0, 0.5, 48.5, 20.0, 5.0, -3.0, 0.0, 0.0, 0.0])


def test_constant_acceleration_matches_the_truth() -> None:
    truth = flight(START, 0, (Leg(1_000_000_000, acceleration=(1.0, 2.0, -1.0)),), [700_000_000])[0]
    np.testing.assert_allclose(transition(CA, 0.7) @ START, truth, rtol=1e-14, atol=1e-12)


@pytest.mark.parametrize("rate", [0.5, -1.3])
def test_a_turn_matches_the_truth_and_composes(rate: float) -> None:
    model = MotionModel(Kind.TURN, 0.5, 0.2, rate)
    truth = flight(START, 0, (Leg(3_000_000_000, turn_rate_rad_s=rate),), [2_300_000_000])[0]
    np.testing.assert_allclose(transition(model, 2.3) @ START, truth, rtol=1e-13, atol=1e-12)
    twice = transition(model, 1.1) @ transition(model, 1.2)
    np.testing.assert_allclose(twice, transition(model, 2.3), rtol=1e-12, atol=1e-12)


def test_a_turn_keeps_speed_and_points_its_acceleration_at_the_centre() -> None:
    moved = transition(LEFT, 0.8) @ START
    assert np.hypot(moved[3], moved[4]) == pytest.approx(np.hypot(START[3], START[4]), rel=1e-14)
    assert moved[6] * moved[3] + moved[7] * moved[4] == pytest.approx(0.0, abs=1e-12)
    assert moved[3] * moved[7] - moved[4] * moved[6] > 0.0


@pytest.mark.parametrize("model", [CV, CA, LEFT])
def test_process_noise_is_symmetric_and_positive_semidefinite(model: MotionModel) -> None:
    noise = process_noise(model, 0.01)
    np.testing.assert_array_equal(noise, noise.T)
    assert np.linalg.eigvalsh(noise).min() >= -ROUNDING
    assert noise.shape == (STATE_SIZE, STATE_SIZE)


def test_white_acceleration_noise_leaves_the_acceleration_alone() -> None:
    noise = process_noise(CV, 0.1)
    assert noise[0, 0] == pytest.approx(0.5 * 0.1**3 / 3.0)
    assert noise[5, 5] == pytest.approx(0.2 * 0.1)
    assert not noise[6:, :].any()


@pytest.mark.parametrize(
    ("kind", "horizontal", "rate"),
    [(Kind.TURN, 1.0, 0.0), (Kind.CONSTANT_VELOCITY, 1.0, 0.5), (Kind.CONSTANT_ACCELERATION, -1.0, 0.0)],
)
def test_unusable_models_are_refused(kind: Kind, horizontal: float, rate: float) -> None:
    with pytest.raises(EndgameError):
        MotionModel(kind, horizontal, 1.0, rate)


def test_a_flight_chains_its_legs_and_continues_the_last() -> None:
    legs = (Leg(1_000_000_000), Leg(1_000_000_000, acceleration=(0.0, 0.0, 2.0)))
    states = flight(START, 0, legs, [0, 1_000_000_000, 3_000_000_000])
    np.testing.assert_allclose(states[1][0:3], [21.0, 3.0, 47.0])
    assert states[0][6] == 0.0
    assert states[2][2] == pytest.approx(47.0 - 3.0 * 2.0 + 2.0 * 2.0 * 2.0 / 2.0)


@pytest.mark.parametrize(("legs", "time"), [((), 0), ((Leg(0),), 0), ((Leg(1),), -1)])
def test_unusable_flights_are_refused(legs: tuple[Leg, ...], time: int) -> None:
    with pytest.raises(EndgameError):
        flight(START, 0, legs, [time])
