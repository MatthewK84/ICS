"""Basic test for kill-class thresholds."""

import numpy as np

from ics_models.killclass.thresholds import recommend_thresholds


def test_recommend_returns_thresholds() -> None:
    truth = np.array([True, False, True])
    n_obs = np.array([4, 1, 5])
    mass = np.array([0.1, 0.01, 0.08])
    result = recommend_thresholds(truth, n_obs, mass)
    assert result.n_fragments >= 2
