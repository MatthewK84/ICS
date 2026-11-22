"""The batch fit of the initial state and κ to timed 3D points (ICS-044).

Each point is a triangulated position in the range ENU frame with the same
standard deviation on every axis. The fit finds the state at the first
point's time that minimises the sum of squared, whitened position residuals,
by Levenberg-Marquardt with central-difference derivatives. The covariance is
the inverse of JᵀJ at the solution, so it is in the state's own units.

The fit is seeded from a quadratic through every point on each axis, which
gives the position and velocity at the first point, and from the settings' κ.
A fit that has not converged within the iteration limit says so in its result
rather than raising; inputs it cannot fit at all raise ValueError. A trial
step whose path leaves the ISA troposphere is refused like one that raises
the cost.
"""

import math
from collections.abc import Sequence
from dataclasses import dataclass
from itertools import pairwise

import numpy as np

from ics_models.ballistic.dynamics import STATE_SIZE, Matrix, Model, Vector, propagate

MIN_POINTS: int = 4
AXES: int = 3
QUADRATIC: int = 2
NANOSECONDS_PER_SECOND: float = 1e9
# The central-difference step for each state element: m, m/s, m²/kg.
DERIVATIVE_STEPS: tuple[float, ...] = (1e-3, 1e-3, 1e-3, 1e-4, 1e-4, 1e-4, 1e-6)
# Convergence: every step element under STEP_TOLERANCE of the element's size
# plus its floor.
STEP_FLOORS: tuple[float, ...] = (1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1e-3)
DAMPING_FACTOR: float = 10.0


@dataclass(frozen=True)
class FitSettings:
    """How the fit runs: the model, the points' sigma per axis, κ's seed, and the stopping rules."""

    model: Model
    sigma_m: float
    initial_kappa_m2_kg: float = 0.05
    max_iterations: int = 100
    step_tolerance: float = 1e-10
    initial_damping: float = 1e-3


@dataclass(frozen=True)
class FitResult:
    """The fitted state at epoch_ns, its covariance, the RMS position residual, and how the fit went."""

    epoch_ns: int
    state: Vector
    covariance: Matrix
    residual_rms_m: float
    iterations: int
    converged: bool


@dataclass(frozen=True)
class Problem:
    """The points to fit, checked, with their times."""

    times_ns: tuple[int, ...]
    points: Matrix
    settings: FitSettings


def checked_problem(times_ns: Sequence[int], points: Matrix, settings: FitSettings) -> Problem:
    """The inputs as a Problem, or ValueError when they cannot be fitted."""
    if points.ndim != QUADRATIC or points.shape[1] != AXES or points.shape[0] != len(times_ns):
        msg = "points must be one row of east, north and up per time"
        raise ValueError(msg)
    if len(times_ns) < MIN_POINTS:
        msg = f"at least {MIN_POINTS} points are needed"
        raise ValueError(msg)
    if any(later <= earlier for earlier, later in pairwise(times_ns)):
        msg = "times must increase"
        raise ValueError(msg)
    if not np.all(np.isfinite(points)) or not settings.sigma_m > 0.0:
        msg = "points must be finite and sigma positive"
        raise ValueError(msg)
    return Problem(tuple(times_ns), points.astype(np.float64), settings)


def seed(problem: Problem) -> Vector:
    """The quadratic seed: position and velocity at the first time, and the settings' κ."""
    seconds = np.array([float(time - problem.times_ns[0]) / NANOSECONDS_PER_SECOND for time in problem.times_ns])
    design = np.stack([np.ones_like(seconds), seconds, seconds * seconds], axis=1)
    coefficients = np.linalg.lstsq(design, problem.points, rcond=None)[0]
    return np.array([*coefficients[0], *coefficients[1], problem.settings.initial_kappa_m2_kg])


def whitened_residuals(predicted: Matrix, problem: Problem) -> Vector:
    """(predicted - measured) / sigma, flattened point by point."""
    return ((predicted - problem.points) / problem.settings.sigma_m).reshape(-1)


def residuals_and_jacobian(state: Vector, problem: Problem) -> tuple[Vector, Matrix]:
    """The whitened residuals at a state, and their central-difference Jacobian."""
    steps = np.diag(np.array(DERIVATIVE_STEPS))
    rows = np.vstack([state.reshape(1, STATE_SIZE), state + steps, state - steps])
    paths = propagate(rows, problem.times_ns[0], problem.times_ns, problem.settings.model)
    residuals = whitened_residuals(paths[:, 0, 0:AXES], problem)
    jacobian = np.empty((residuals.size, STATE_SIZE))
    for column in range(STATE_SIZE):
        plus = paths[:, 1 + column, 0:AXES].reshape(-1)
        minus = paths[:, 1 + STATE_SIZE + column, 0:AXES].reshape(-1)
        jacobian[:, column] = (plus - minus) / (2.0 * DERIVATIVE_STEPS[column] * problem.settings.sigma_m)
    return residuals, jacobian


def cost(state: Vector, problem: Problem) -> float:
    """The sum of squared whitened residuals at a state, or infinity if its path leaves the atmosphere."""
    try:
        paths = propagate(state.reshape(1, STATE_SIZE), problem.times_ns[0], problem.times_ns, problem.settings.model)
    except ValueError:
        return math.inf
    residuals = whitened_residuals(paths[:, 0, 0:AXES], problem)
    return float(residuals @ residuals)


def damped_step(residuals: Vector, jacobian: Matrix, damping: float) -> Vector:
    """The Levenberg-Marquardt step, with Marquardt's diagonal scaling."""
    normal = jacobian.T @ jacobian
    damped = normal + damping * np.diag(np.diag(normal))
    return np.asarray(np.linalg.solve(damped, -(jacobian.T @ residuals)), dtype=np.float64)


def is_small(step: Vector, state: Vector, tolerance: float) -> bool:
    """Whether every step element is under tolerance of its state element's size plus floor."""
    return bool(np.all(np.abs(step) <= tolerance * (np.abs(state) + np.array(STEP_FLOORS))))


def fit(times_ns: Sequence[int], points: Matrix, settings: FitSettings) -> FitResult:
    """Fit the state at the first time, and κ, to the points."""
    problem = checked_problem(times_ns, points, settings)
    state, damping, converged, iterations = seed(problem), settings.initial_damping, False, 0
    while iterations < settings.max_iterations and not converged:
        iterations += 1
        residuals, jacobian = residuals_and_jacobian(state, problem)
        step = damped_step(residuals, jacobian, damping)
        trial = state + step
        if cost(trial, problem) <= float(residuals @ residuals):
            state, damping = trial, damping / DAMPING_FACTOR
            converged = is_small(step, state, settings.step_tolerance)
        else:
            damping *= DAMPING_FACTOR
    return finished(state, problem, iterations, converged)


def finished(state: Vector, problem: Problem, iterations: int, converged: bool) -> FitResult:
    """The result at the final state: its covariance and RMS residual."""
    residuals, jacobian = residuals_and_jacobian(state, problem)
    covariance = np.asarray(np.linalg.inv(jacobian.T @ jacobian), dtype=np.float64)
    rms = float(np.sqrt(residuals @ residuals / (residuals.size / AXES))) * problem.settings.sigma_m
    return FitResult(problem.times_ns[0], state, covariance, rms, iterations, converged)
