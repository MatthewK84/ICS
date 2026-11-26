"""Turbulence estimate from image jitter."""

import numpy as np


def turbulence_from_jitter(jitter_rad: float, range_m: float) -> float:
    """Estimate C_n^2 from observed angular jitter.

    Returns a representative refractive-index structure constant.
    """
    if range_m <= 0.0 or jitter_rad <= 0.0:
        return 0.0
    # Simplified relation for the reference
    return (jitter_rad / range_m) ** 2 * 1e-13
