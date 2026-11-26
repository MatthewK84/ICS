"""ROC-based threshold recommendation."""

from dataclasses import dataclass

import numpy as np
import numpy.typing as npt

DEFAULT_TIME_S: float = 1.0
DEFAULT_RESIDUAL_M: float = 2.0


@dataclass(frozen=True)
class Thresholds:
    n_fragments: int
    mass_kg: float
    time_s: float
    residual_m: float


def recommend_thresholds(
    truth_kills: npt.NDArray[np.bool_],
    n_obs: npt.NDArray[np.int_],
    mass_obs: npt.NDArray[np.float64],
    time_obs: npt.NDArray[np.float64],
    residual_obs: npt.NDArray[np.float64],
) -> Thresholds:
    """Recommend thresholds that maximise a simple Youden index on synthetic data."""
    best = Thresholds(3, 0.05, DEFAULT_TIME_S, DEFAULT_RESIDUAL_M)
    best_score = -1.0
    for n in (2, 3, 5):
        for m in (0.02, 0.05, 0.1):
            pred = (n_obs >= n) & (mass_obs >= m)
            tp = np.sum(pred & truth_kills)
            fn = np.sum(~pred & truth_kills)
            fp = np.sum(pred & ~truth_kills)
            score = tp / max(tp + fn, 1) - fp / max(np.sum(~truth_kills), 1)
            if score > best_score:
                best_score = score
                best = Thresholds(n, m, DEFAULT_TIME_S, DEFAULT_RESIDUAL_M)
    return best
