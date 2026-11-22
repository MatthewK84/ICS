"""Model selection by BIC (ICS-045)."""

import math

import numpy as np
import pytest

from ics_models.ballistic.dynamics import Model, trajectory
from ics_models.ballistic.fit import BALLISTIC, FitResult, FitSettings
from ics_models.ballistic.select import bic, select

START_NS: int = 5_000_000_000
MODEL = Model(origin_height_m=1_200.0)
SETTINGS = FitSettings(model=MODEL, sigma_m=0.02)
TIMES: tuple[int, ...] = tuple(START_NS + step * 10_000_000 for step in range(101))


def result(rms_m: float, converged: bool) -> FitResult:
    return FitResult(START_NS, np.zeros(7), np.eye(7), rms_m, 3, converged)


def points() -> np.ndarray:
    truth = np.array([10.0, -20.0, 150.0, 6.0, -3.0, 4.0, 0.1])
    return trajectory(truth, START_NS, TIMES, MODEL)[:, 0:3]


def test_bic_is_chi_squared_plus_a_penalty_per_parameter() -> None:
    # An RMS of one sigma over 100 points is a chi-squared of 100, from 300 residuals.
    assert bic(result(0.02, converged=True), 7, 100, 0.02) == pytest.approx(100.0 + 7.0 * math.log(300.0))


def test_a_fit_that_did_not_converge_scores_infinite() -> None:
    assert bic(result(0.0, converged=False), 7, 100, 0.02) == math.inf


def test_the_only_converged_candidate_is_chosen() -> None:
    selection = select(TIMES, points(), SETTINGS, (BALLISTIC,))
    assert selection.chosen == "ballistic"
    assert selection.scored[0].result.converged


def test_nothing_is_chosen_when_no_fit_converges() -> None:
    hopeless = FitSettings(model=MODEL, sigma_m=0.02, max_iterations=1)
    assert select(TIMES, points(), hopeless, (BALLISTIC,)).chosen is None


def test_there_must_be_a_candidate() -> None:
    with pytest.raises(ValueError, match="candidate"):
        select(TIMES, points(), SETTINGS, ())
