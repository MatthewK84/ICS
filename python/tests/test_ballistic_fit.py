"""The batch fit of the 7-state ballistic model: it recovers synthetic truth (ICS-044 done_when)."""

import math

import numpy as np
import pytest

from ics_models.ballistic.dynamics import STATE_SIZE, Model, trajectory
from ics_models.ballistic.fit import FitSettings, checked_problem, cost, fit

START_NS: int = 7_000_000_000
POINT_NS: int = 10_000_000
MODEL = Model(origin_height_m=1_200.0)
SIGMA_M: float = 0.02
SETTINGS = FitSettings(model=MODEL, sigma_m=SIGMA_M)
TRIALS: int = 30
CHI2_7_LOW: float = 5.0
CHI2_7_HIGH: float = 9.0
NOISE_FREE_RMS_M: float = 1e-9


def times(count: int) -> list[int]:
    return [START_NS + point * POINT_NS for point in range(count)]


def truth(kappa: float) -> np.ndarray:
    return np.array([30.0, -40.0, 120.0, -7.0, 9.0, 5.0, kappa])


@pytest.mark.parametrize("kappa", [0.0, 0.005, 0.02, 0.08, 0.3, 1.0])
def test_noise_free_points_give_back_the_truth(kappa: float) -> None:
    when = times(201)
    result = fit(when, trajectory(truth(kappa), START_NS, when, MODEL)[:, 0:3], SETTINGS)
    assert result.converged
    assert result.epoch_ns == START_NS
    assert np.allclose(result.state, truth(kappa), rtol=1e-6, atol=1e-6 * 1e-3)
    assert result.residual_rms_m < NOISE_FREE_RMS_M


def test_noisy_fits_are_consistent_with_their_covariance() -> None:
    rng = np.random.default_rng(4407)
    when = times(151)
    squared: list[float] = []
    kappa_within_3_sigma = 0
    for _ in range(TRIALS):
        piece = np.array([*rng.uniform(-80.0, 80.0, 2), rng.uniform(60.0, 180.0), *rng.uniform(-12.0, 12.0, 3)])
        state = np.array([*piece, rng.uniform(0.0, 0.5)])
        path = trajectory(state, START_NS, when, MODEL)[:, 0:3]
        result = fit(when, path + rng.normal(0.0, SIGMA_M, path.shape), SETTINGS)
        assert result.converged
        error = result.state - state
        squared.append(float(error @ np.linalg.solve(result.covariance, error)))
        kappa_within_3_sigma += int(abs(error[6]) <= 3.0 * np.sqrt(result.covariance[6, 6]))
        assert result.residual_rms_m == pytest.approx(SIGMA_M * np.sqrt(3.0), rel=0.2)
    assert CHI2_7_LOW < float(np.mean(squared)) < CHI2_7_HIGH
    assert kappa_within_3_sigma >= TRIALS - 2


def test_a_fit_out_of_iterations_says_it_has_not_converged() -> None:
    when = times(50)
    points = trajectory(truth(0.3), START_NS, when, MODEL)[:, 0:3]
    result = fit(when, points, FitSettings(model=MODEL, sigma_m=SIGMA_M, max_iterations=1))
    assert not result.converged
    assert result.iterations == 1
    assert result.covariance.shape == (STATE_SIZE, STATE_SIZE)


def test_a_trial_state_whose_path_leaves_the_atmosphere_costs_infinity() -> None:
    when = times(10)
    problem = checked_problem(when, trajectory(truth(0.3), START_NS, when, MODEL)[:, 0:3], SETTINGS)
    assert cost(truth(0.3), problem) < NOISE_FREE_RMS_M
    assert cost(np.array([0.0, 0.0, 20_000.0, 0.0, 0.0, 0.0, 0.3]), problem) == math.inf


def test_inputs_that_cannot_be_fitted_are_refused() -> None:
    points = np.zeros((5, 3))
    with pytest.raises(ValueError, match="one row of east"):
        fit(times(4), points, SETTINGS)
    with pytest.raises(ValueError, match="at least 4"):
        fit(times(3), points[0:3], SETTINGS)
    with pytest.raises(ValueError, match="must increase"):
        fit([START_NS, START_NS, START_NS + 1, START_NS + 2, START_NS + 3], points, SETTINGS)
    with pytest.raises(ValueError, match="finite and sigma"):
        fit(times(5), np.full((5, 3), np.nan), SETTINGS)
    with pytest.raises(ValueError, match="finite and sigma"):
        fit(times(5), points, FitSettings(model=MODEL, sigma_m=0.0))
