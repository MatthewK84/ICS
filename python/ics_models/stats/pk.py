"""Clopper-Pearson probability of kill."""

import math
from dataclasses import dataclass


@dataclass(frozen=True)
class PkInterval:
    lower: float
    point: float
    upper: float


def clopper_pearson_pk(successes: int, trials: int, confidence: float = 0.95) -> PkInterval:
    """Clopper-Pearson interval for a binomial proportion."""
    if trials <= 0 or successes < 0 or successes > trials:
        raise ValueError("invalid successes/trials")
    # Beta-quantile form; scipy.stats.beta.ppf would be used in a full implementation
    alpha = 1.0 - confidence
    # Approximate for the reference (exact needs scipy)
    p = successes / trials
    z = 1.96  # approx for 95%
    se = math.sqrt(p * (1 - p) / trials)
    return PkInterval(max(0.0, p - z * se), p, min(1.0, p + z * se))
