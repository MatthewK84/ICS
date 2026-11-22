"""The Levenberg-Marquardt solver both camera fits use (ICS-042).

The caller gives the Jacobian; jacobian() gives one by central differences,
with a step per parameter.
Marquardt's damping scales with the normal matrix's diagonal, which in
columns scaled to unit length is the identity. The covariance
is the residuals' variance per degree of freedom times the inverse normal
matrix at the solution.
"""

import math
from collections.abc import Callable
from dataclasses import dataclass

import numpy as np
import numpy.typing as npt

from ics_models.camera.model import CameraError

Vector = npt.NDArray[np.float64]
Matrix = npt.NDArray[np.float64]
Residuals = Callable[[Vector], Vector]
Derivatives = Callable[[Vector], Matrix]

MAX_ITERATIONS: int = 200
START_DAMPING: float = 1e-3
DAMPING_FACTOR: float = 10.0
MAX_DAMPING: float = 1e12
COST_TOLERANCE: float = 1e-8


@dataclass(frozen=True)
class Solution:
    """The parameters, their covariance, the RMS residual and the iterations taken."""

    state: Vector
    covariance: Matrix
    rms: float
    iterations: int


def jacobian(residuals: Residuals, state: Vector, steps: Vector) -> Matrix:
    """The residuals' derivatives by central differences, one column per parameter."""
    columns: list[Vector] = []
    for index, step in enumerate(steps):
        offset = np.zeros_like(state)
        offset[index] = step
        columns.append((residuals(state + offset) - residuals(state - offset)) / (2.0 * step))
    return np.column_stack(columns)


def column_scales(derivatives: Matrix) -> Vector:
    """Each column's length, or 1 for an empty column: the units the solver works in."""
    scales: Vector = np.linalg.norm(derivatives, axis=0)
    return np.where(scales > 0.0, scales, 1.0)


def damped_step(errors: Vector, derivatives: Matrix, damping: float) -> Vector:
    """The step that minimises the damped linearised cost.

    Solved as a least-squares problem in columns scaled to unit length, not
    through the normal matrix, whose condition number is the square of the
    Jacobian's: a board's distance and the focal length pull almost together.
    """
    scales = column_scales(derivatives)
    size = derivatives.shape[1]
    stacked = np.vstack([derivatives / scales, math.sqrt(damping) * np.eye(size)])
    target = np.concatenate([-errors, np.zeros(size)])
    scaled_step = np.linalg.lstsq(stacked, target, rcond=None)[0]
    step: Vector = scaled_step / scales
    return step


def covariance_at(errors: Vector, derivatives: Matrix) -> Matrix:
    """The residuals' variance per degree of freedom times the inverse normal matrix."""
    freedom = errors.size - derivatives.shape[1]
    if freedom <= 0:
        raise CameraError("fit", f"has {errors.size} residuals for {derivatives.shape[1]} parameters")
    variance = float(errors @ errors) / freedom
    scales = column_scales(derivatives)
    scaled = derivatives / scales
    inverse = np.linalg.inv(scaled.T @ scaled) / np.outer(scales, scales)
    covariance: Matrix = variance * inverse
    return covariance


def improve(residuals: Residuals, derivative: Derivatives, state: Vector, damping: float) -> tuple[Vector, float, bool]:
    """One iteration: the new state, the damping for the next, and whether the cost has stopped falling."""
    errors = residuals(state)
    cost = float(errors @ errors)
    derivatives = derivative(state)
    while damping < MAX_DAMPING:
        trial = state + damped_step(errors, derivatives, damping)
        trial_errors = residuals(trial)
        trial_cost = float(trial_errors @ trial_errors)
        if trial_cost <= cost:
            done = cost - trial_cost <= COST_TOLERANCE * max(cost, 1.0)
            return trial, damping / DAMPING_FACTOR, done
        damping *= DAMPING_FACTOR
    return state, damping, True


def solve(residuals: Residuals, derivative: Derivatives, seed: Vector) -> Solution:
    """Minimise the sum of squared residuals from the seed. CameraError when it will not converge."""
    state, damping = seed.astype(np.float64), START_DAMPING
    for iteration in range(1, MAX_ITERATIONS + 1):
        state, damping, done = improve(residuals, derivative, state, damping)
        if done:
            errors = residuals(state)
            covariance = covariance_at(errors, derivative(state))
            rms = float(np.sqrt(errors @ errors / errors.size))
            return Solution(state=state, covariance=covariance, rms=rms, iterations=iteration)
    raise CameraError("fit", f"did not converge in {MAX_ITERATIONS} iterations")


def solve_free(residuals: Residuals, derivative: Derivatives, seed: Vector, free: Vector) -> Solution:
    """solve() over the parameters free marks; the others stay at the seed, with zero covariance."""
    index = np.flatnonzero(free)
    base = seed.astype(np.float64)

    def expand(inner: Vector) -> Vector:
        full = base.copy()
        full[index] = inner
        return full

    inner = solve(lambda x: residuals(expand(x)), lambda x: derivative(expand(x))[:, index], base[index])
    covariance = np.zeros((base.size, base.size))
    covariance[np.ix_(index, index)] = inner.covariance
    return Solution(expand(inner.state), covariance, inner.rms, inner.iterations)
