"""Basic test for uncertainty propagation."""

import numpy as np

from ics_models.uncertainty.propagate import ErrorBudget, sigma_versus_range


def test_sigma_increases_with_range() -> None:
    ranges = np.array([100.0, 1000.0])
    sigmas = sigma_versus_range(ranges, ErrorBudget())
    assert sigmas[1] > sigmas[0]
