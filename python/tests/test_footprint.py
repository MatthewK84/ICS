"""Basic tests for the footprint Monte Carlo reference."""

import numpy as np

from ics_models.ballistic.dynamics import Model
from ics_models.footprint.propagate import Fragment, run_monte_carlo
from ics_models.footprint.regions import compute_regions
from ics_models.footprint.wind import WindProfile


def test_monte_carlo_and_regions() -> None:
    rng = np.random.default_rng(42)
    model = Model(origin_height_m=100.0)
    wind = WindProfile(
        height_m=np.array([0.0, 1000.0]),
        east_mps=np.array([2.0, 5.0]),
        north_mps=np.array([1.0, 2.0]),
    )
    state = np.zeros(7)
    state[2] = 50.0
    state[6] = 0.05
    frag = Fragment(state=state, mass_kg=0.1, cd=1.0, area_m2=0.01)
    result = run_monte_carlo([frag], wind, 10, model, rng)
    regions = compute_regions(result.impacts_en_m)
    assert regions.p90.shape[0] >= 2
