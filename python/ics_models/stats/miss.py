"""Miss-distance summaries."""

from dataclasses import dataclass

import numpy as np
import numpy.typing as npt


@dataclass(frozen=True)
class MissSummary:
    median_m: float
    p90_m: float
    mean_m: float


def miss_distance_summary(misses_m: npt.NDArray[np.float64]) -> MissSummary:
    """Median, 90th percentile and mean miss distance."""
    if misses_m.size == 0:
        return MissSummary(0.0, 0.0, 0.0)
    return MissSummary(
        float(np.median(misses_m)),
        float(np.percentile(misses_m, 90)),
        float(np.mean(misses_m)),
    )
