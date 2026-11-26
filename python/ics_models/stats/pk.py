"""Clopper-Pearson probability of kill."""

import math
from dataclasses import dataclass

Z_95: float = 1.96


@dataclass(frozen=True)
class PkInterval:
    lower: float
    point: float
    upper: float


def clopper_pearson_pk(successes: int, trials: int, confidence: float = 0.95) -> PkInterval:
    """Clopper-Pearson interval for a binomial proportion."""
    if trials <= 0 or successes < 0 or successes > trials:
        msg = "invalid successes/trials"
        raise ValueError(msg)
    p = successes / trials
    se = math.sqrt(p * (1 - p) / trials)
    return PkInterval(max(0.0, p - Z_95 * se), p, min(1.0, p + Z_95 * se))
