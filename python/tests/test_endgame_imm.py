"""The IMM two-body tracker (ICS-046), on the scripted engagement of golden/imm.

The done-when criterion: fed noise-free positions, the tracked miss distance
is within 1 mm of the truth's; fed noisy ones, it is within 3 sigma.
"""

import math

import numpy as np
import pytest

from ics_golden.imm import (
    INTERCEPTOR_LEGS,
    MEASUREMENT_NS,
    MODELS,
    SETTINGS,
    START_NS,
    TARGET_LEGS,
    TARGET_START,
    TRUE_CLOSEST_NS,
    TRUE_MISS_M,
    engagement,
    interceptor_start,
)
from ics_models.cue.kinematics import STATE_SIZE, Estimate
from ics_models.endgame.approach import EncounterSettings, encounter
from ics_models.endgame.imm import ImmSettings, ImmState, Measurement, combined, predict, start, update
from ics_models.endgame.motion import EndgameError, Kind, MotionModel
from ics_models.endgame.truth import flight

ENCOUNTER = EncounterSettings(horizon_ns=1_000_000_000, contact_radius_m=0.5)
MILLIMETRE: float = 1e-3
LEG_MEASUREMENTS: int = 200
SETTLE: int = 50
TIME_NS: int = 10_000
MOSTLY: float = 0.6


def final_encounter(sigma_m: float, noisy: bool = True) -> tuple[float, float, int]:
    """The tracked engagement's last miss, its sigma and time."""
    _, target, interceptor = engagement(np.random.default_rng(7), sigma_m, noisy)
    found = encounter(combined(target[-1]), combined(interceptor[-1]), ENCOUNTER)
    assert found is not None
    return found.miss_m, found.miss_sigma_m, found.time_ns


def test_the_scripted_truth_is_closest_when_and_where_it_says() -> None:
    times = [START_NS + TRUE_CLOSEST_NS + offset for offset in range(-1_000_000, 1_000_001, 1_000)]
    targets = flight(np.array(TARGET_START), START_NS, TARGET_LEGS, times)
    interceptors = flight(interceptor_start(), START_NS, INTERCEPTOR_LEGS, times)
    gaps = [float(np.linalg.norm(b[0:3] - a[0:3])) for a, b in zip(targets, interceptors, strict=True)]
    assert times[int(np.argmin(gaps))] == START_NS + TRUE_CLOSEST_NS
    assert min(gaps) == pytest.approx(TRUE_MISS_M, abs=1e-9)


def test_fed_noise_free_positions_the_miss_is_within_a_millimetre() -> None:
    miss, _, time_ns = final_encounter(0.001, noisy=False)
    assert abs(miss - TRUE_MISS_M) <= MILLIMETRE
    assert abs(time_ns - (START_NS + TRUE_CLOSEST_NS)) <= TIME_NS


@pytest.mark.parametrize("sigma_m", [0.001, 0.005, 0.02])
def test_fed_noisy_positions_the_miss_is_within_three_sigma(sigma_m: float) -> None:
    miss, sigma, _ = final_encounter(sigma_m)
    assert 0.0 < sigma < 10.0 * sigma_m
    assert abs(miss - TRUE_MISS_M) <= 3.0 * sigma


def test_the_target_s_model_follows_its_manoeuvres() -> None:
    _, target, _ = engagement(np.random.default_rng(3), 0.005)
    # Straight, then a left turn, then an acceleration: models 0, 2 and 1.
    for leg, model in enumerate((0, 2, 1)):
        chances = [
            state.probabilities[model]
            for state in target[leg * LEG_MEASUREMENTS + SETTLE : (leg + 1) * LEG_MEASUREMENTS]
        ]
        assert float(np.mean(chances)) > MOSTLY, leg


def test_chances_stay_chances_and_covariances_stay_symmetric() -> None:
    _, target, interceptor = engagement(np.random.default_rng(5), 0.005)
    for state in (*target, *interceptor):
        assert math.fsum(state.probabilities) == pytest.approx(1.0, abs=1e-12)
        estimate = combined(state)
        np.testing.assert_allclose(estimate.covariance, estimate.covariance.T, rtol=1e-12, atol=1e-15)


def seeded(settings: ImmSettings = SETTINGS) -> ImmState:
    return start(Estimate(START_NS, np.array(TARGET_START), np.eye(STATE_SIZE)), settings)


def test_prediction_mixes_but_does_not_measure() -> None:
    predicted = predict(seeded(), START_NS + MEASUREMENT_NS, SETTINGS)
    np.testing.assert_allclose(predicted.probabilities, SETTINGS.initial)
    assert predicted.time_ns == START_NS + MEASUREMENT_NS
    assert predicted.estimates[0].state[0] == pytest.approx(0.2)


def test_a_model_no_one_switches_to_keeps_its_own_estimate() -> None:
    settings = ImmSettings(MODELS[0:2], ((1.0, 0.0), (0.0, 1.0)), (1.0, 0.0))
    state = start(Estimate(START_NS, np.array(TARGET_START), np.eye(STATE_SIZE)), settings)
    predicted = predict(state, START_NS + MEASUREMENT_NS, settings)
    assert predicted.probabilities[1] == 0.0
    assert predicted.estimates[1].state[0] == pytest.approx(0.2)


def measurement(time_ns: int, position: tuple[float, float, float], sigma: float = 0.01) -> Measurement:
    return Measurement(time_ns, np.array(position), np.eye(3) * sigma * sigma)


def test_a_wild_measurement_does_not_underflow_the_chances() -> None:
    state = update(seeded(), measurement(START_NS + MEASUREMENT_NS, (1.0e4, 0.0, 100.0), 1e-3), SETTINGS)
    assert math.fsum(state.probabilities) == pytest.approx(1.0)


@pytest.mark.parametrize(
    ("time_ns", "position", "covariance"),
    [
        (START_NS - 1, np.zeros(3), np.eye(3)),
        (START_NS + 1, np.zeros(2), np.eye(3)),
        (START_NS + 1, np.zeros(3), np.eye(2)),
        (START_NS + 1, np.zeros(3), -np.eye(3) * 10.0),
    ],
    ids=["earlier", "short position", "short covariance", "negative covariance"],
)
def test_unusable_measurements_are_refused(time_ns: int, position: np.ndarray, covariance: np.ndarray) -> None:
    with pytest.raises(EndgameError):
        update(seeded(), Measurement(time_ns, position, covariance), SETTINGS)


def test_measurements_no_model_can_explain_are_refused() -> None:
    settings = ImmSettings(MODELS[0:2], ((1.0, 0.0), (0.0, 1.0)), (1.0, 0.0))
    state = start(Estimate(START_NS, np.array(TARGET_START), np.eye(STATE_SIZE)), settings)
    broken = ImmState(state.estimates, np.zeros(2))
    with pytest.raises(EndgameError):
        update(broken, measurement(START_NS + 1, (0.0, 0.0, 100.0)), settings)


def test_a_seed_of_the_wrong_size_is_refused() -> None:
    with pytest.raises(EndgameError):
        start(Estimate(START_NS, np.zeros(6), np.eye(6)), SETTINGS)


CV = MotionModel(Kind.CONSTANT_VELOCITY, 1.0, 1.0)


@pytest.mark.parametrize(
    ("models", "switching", "initial"),
    [
        ((), (), ()),
        ((CV, CV), ((1.0, 0.0),), (1.0, 0.0)),
        ((CV, CV), ((0.5, 0.6), (0.0, 1.0)), (1.0, 0.0)),
        ((CV, CV), ((1.0, 0.0), (0.0, 1.0)), (1.5, -0.5)),
        ((CV, CV), ((1.0, 0.0), (0.0, 1.0)), (1.0,)),
    ],
)
def test_unusable_settings_are_refused(
    models: tuple[MotionModel, ...], switching: tuple[tuple[float, ...], ...], initial: tuple[float, ...]
) -> None:
    with pytest.raises(EndgameError):
        ImmSettings(models, switching, initial)
