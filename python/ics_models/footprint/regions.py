"""50/90/99% containment regions from impact samples."""

from dataclasses import dataclass

import numpy as np
import numpy.typing as npt
from scipy.spatial import ConvexHull


@dataclass(frozen=True)
class ContainmentRegions:
    """Convex hulls (or empty) for the three probability levels."""

    p50: npt.NDArray[np.float64]
    p90: npt.NDArray[np.float64]
    p99: npt.NDArray[np.float64]


def compute_regions(impacts: npt.NDArray[np.float64]) -> ContainmentRegions:
    """Return convex hulls of the central 50/90/99% of samples by distance from median."""
    if impacts.shape[0] < 3:
        empty = np.empty((0, 2))
        return ContainmentRegions(empty, empty, empty)
    median = np.median(impacts, axis=0)
    dist = np.linalg.norm(impacts - median, axis=1)
    order = np.argsort(dist)

    def hull(fraction: float) -> npt.NDArray[np.float64]:
        n = max(3, int(fraction * impacts.shape[0]))
        pts = impacts[order[:n]]
        try:
            return pts[ConvexHull(pts).vertices]
        except Exception:
            return pts

    return ContainmentRegions(p50=hull(0.50), p90=hull(0.90), p99=hull(0.99))
