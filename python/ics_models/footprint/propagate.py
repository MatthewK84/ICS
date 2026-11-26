"""Fragment propagation with sampled uncertainty."""

from dataclasses import dataclass

import numpy as np
import numpy.typing as npt

from ics_models.ballistic.dynamics import Model, trajectory
from ics_models.footprint.wind import WindProfile


@dataclass(frozen=True)
class Fragment:
    """A debris fragment with initial state and uncertainty."""

    state: npt.NDArray[np.float64]
    mass_kg: float
    cd: float
    area_m2: float


@dataclass(frozen=True)
class FootprintResult:
    """Impact points (east, north) from one Monte Carlo run."""

    impacts_en_m: npt.NDArray[np.float64]


def run_monte_carlo(
    fragments: list[Fragment],
    wind: WindProfile,
    n_samples: int,
    model: Model,
    rng: np.random.Generator,
) -> FootprintResult:
    """Propagate each fragment n_samples times with sampled initial uncertainty and wind."""
    impacts: list[npt.NDArray[np.float64]] = []
    for frag in fragments:
        for _ in range(n_samples):
            noise = rng.normal(0.0, 1.0, size=6)
            state = frag.state.copy()
            state[0:3] += noise[0:3] * 5.0
            state[3:6] += noise[3:6] * 1.0
            h = model.origin_height_m + state[2]
            we, wn = wind.at(h)
            state[3] += we
            state[4] += wn
            times = [int(t) for t in np.arange(0, 30_000_000_000, model.step_ns)]
            traj = trajectory(state, 0, times, model)
            impact_idx = int(np.argmax(traj[:, 2] <= 0.0))
            if traj[impact_idx, 2] > 0.0:
                impact_idx = len(traj) - 1
            impacts.append(traj[impact_idx, 0:2].astype(np.float64))
    return FootprintResult(impacts_en_m=np.asarray(impacts, dtype=np.float64))
