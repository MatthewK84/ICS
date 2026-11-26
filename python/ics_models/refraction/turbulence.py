"""Turbulence estimate from image jitter."""


def turbulence_from_jitter(jitter_rad: float, range_m: float) -> float:
    """Return a simple turbulence strength estimate from image jitter.

    Returns 0 when inputs are non-positive.
    """
    if jitter_rad <= 0.0 or range_m <= 0.0:
        return 0.0
    return jitter_rad * range_m
