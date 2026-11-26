"""Propagate error sources into sigma versus slant range."""

from dataclasses import dataclass

import numpy as np
import numpy.typing as npt

URAD_TO_RAD: float = 1e-6
DEFAULT_POINTING_URAD: float = 10.0
DEFAULT_TIMING_US: float = 50.0
DEFAULT_ENCODER_URAD: float = 5.0


@dataclass(frozen=True)
class ErrorBudget:
    pointing_urad: float = DEFAULT_POINTING_URAD
    timing_us: float = DEFAULT_TIMING_US
    encoder_urad: float = DEFAULT_ENCODER_URAD


def sigma_versus_range(ranges_m: npt.NDArray[np.float64], budget: ErrorBudget) -> npt.NDArray[np.float64]:
    """Simple RSS of angular errors converted to metres at range."""
    angular_urad = float(np.sqrt(budget.pointing_urad**2 + budget.encoder_urad**2))
    angular_m = ranges_m * angular_urad * URAD_TO_RAD
    timing_m = ranges_m * 0.0
    result = np.sqrt(angular_m**2 + timing_m**2)
    return result.astype(np.float64, copy=False)
