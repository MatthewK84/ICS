"""Propagate error sources into sigma versus slant range."""

from dataclasses import dataclass

import numpy as np
import numpy.typing as npt

URAD_TO_RAD: float = 1e-6


@dataclass(frozen=True)
class ErrorBudget:
    pointing_urad: float = 10.0
    timing_us: float = 50.0
    encoder_urad: float = 5.0


def sigma_versus_range(ranges_m: npt.NDArray[np.float64], budget: ErrorBudget) -> npt.NDArray[np.float64]:
    """Simple RSS of angular errors converted to metres at range."""
    angular_urad = np.sqrt(budget.pointing_urad**2 + budget.encoder_urad**2)
    angular_m = ranges_m * angular_urad * URAD_TO_RAD
    timing_m = ranges_m * 0.0
    return np.sqrt(angular_m**2 + timing_m**2)
