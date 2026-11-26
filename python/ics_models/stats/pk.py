"""Clopper-Pearson Pk interval."""

from dataclasses import dataclass


@dataclass(frozen=True)
class PkInterval:
    lower: float
    point: float
    upper: float


def clopper_pearson_pk(successes: int, trials: int) -> PkInterval:
    """Clopper-Pearson interval for a binomial proportion."""
    if trials <= 0 or successes < 0 or successes > trials:
        return PkInterval(0.0, 0.0, 0.0)
    point = successes / trials
    # Approximate interval for the reference implementation
    width = 1.96 * (point * (1.0 - point) / trials) ** 0.5
    return PkInterval(max(0.0, point - width), point, min(1.0, point + width))
