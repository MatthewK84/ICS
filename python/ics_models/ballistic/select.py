"""Model selection by residual (ICS-045, PY-04).

Every candidate model is fitted to the same points, and each fit is scored by
the Bayesian information criterion, BIC = χ² + k·ln(n): χ² is the sum of
squared whitened residuals, k the model's state size and n the number of
residuals (three per point). The lowest score wins, so a model with more
parameters must lower χ² by ln(n) for each one to be chosen; a raw residual
threshold would always choose the largest model. A fit that has not
converged is scored infinite and never chosen.
"""

import math
from collections.abc import Sequence
from dataclasses import dataclass

from ics_models.ballistic.aero import FLUTTER, LIFT
from ics_models.ballistic.dynamics import Matrix
from ics_models.ballistic.fit import AXES, BALLISTIC, FitModel, FitResult, FitSettings, fit_with

CANDIDATES: tuple[FitModel, ...] = (BALLISTIC, FLUTTER, LIFT)


@dataclass(frozen=True)
class Scored:
    """One candidate's fit and its BIC."""

    name: str
    result: FitResult
    bic: float


@dataclass(frozen=True)
class Selection:
    """Every candidate's scored fit, in candidate order, and the chosen model's name, or None if none converged."""

    scored: tuple[Scored, ...]
    chosen: str | None


def bic(result: FitResult, size: int, points: int, sigma_m: float) -> float:
    """A fit's BIC, or infinity if it did not converge."""
    if not result.converged:
        return math.inf
    residuals = points * AXES
    chi_squared = residuals * (result.residual_rms_m / sigma_m) ** 2 / AXES
    return chi_squared + size * math.log(residuals)


def select(
    times_ns: Sequence[int], points: Matrix, settings: FitSettings, candidates: tuple[FitModel, ...] = CANDIDATES
) -> Selection:
    """Fit each candidate to the points and choose the one with the lowest BIC."""
    if not candidates:
        msg = "there must be at least one candidate model"
        raise ValueError(msg)
    scored = tuple(
        Scored(model.name, result, bic(result, model.law.size, len(times_ns), settings.sigma_m))
        for model, result in ((model, fit_with(model, times_ns, points, settings)) for model in candidates)
    )
    best = min(scored, key=lambda entry: entry.bic)
    return Selection(scored, best.name if math.isfinite(best.bic) else None)
