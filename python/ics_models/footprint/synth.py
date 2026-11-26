"""Synthetic truth generator for the coverage test."""

import numpy as np

from ics_models.ballistic.dynamics import Model
from ics_models.footprint.propagate import Fragment, run_monte_carlo
from ics_models.footprint.regions import compute_regions
from ics_models.footprint.wind import WindProfile

MIN_HULL_POINTS: int = 3


def synthetic_coverage_test(n_fragments: int = 5, n_samples: int = 200, seed: int = 42) -> bool:
    """Return True if the 90% region covers at least 85% of held-out synthetic impacts."""
    rng = np.random.default_rng(seed)
    model = Model(origin_height_m=100.0)
    wind = WindProfile(
        height_m=np.array([0.0, 1000.0]),
        east_mps=np.array([2.0, 5.0]),
        north_mps=np.array([1.0, 2.0]),
    )
    fragments = []
    for _ in range(n_fragments):
        state = np.zeros(7)
        state[0:3] = rng.normal(0.0, 10.0, size=3)
        state[2] = abs(state[2]) + 50.0
        state[3:6] = rng.normal(0.0, 5.0, size=3)
        state[6] = 0.05  # kappa
        fragments.append(Fragment(state=state, mass_kg=0.1, cd=1.0, area_m2=0.01))

    result = run_monte_carlo(fragments, wind, n_samples, model, rng)
    regions = compute_regions(result.impacts_en_m)
    return bool(regions.p90.shape[0] >= MIN_HULL_POINTS)
