"""The interacting multiple model filter for one body (ICS-046).

Each update has the standard four steps (Blom and Bar-Shalom, 1988):

1. interaction: the chance c_j = Σ_i π_ij·μ_i of each model j after the
   switch, and each model's starting estimate, mixed from all of them with
   weights π_ij·μ_i / c_j and their spread;
2. prediction: each model predicts its mixed estimate to the measurement;
3. filtering: each model takes the measurement, a timed position with its
   covariance, by the Kalman gain in Joseph form, and scores it by the
   Gaussian likelihood of its innovation;
4. combination: μ_j ∝ c_j·Λ_j, and the combined estimate is the mixture's
   mean and covariance.

The likelihoods are kept as logarithms and shifted by the largest, so a
measurement far from every model cannot underflow them all to zero. π is the
Markov switching matrix: row i gives the chances of moving from model i.
"""

import math
from collections.abc import Sequence
from dataclasses import dataclass

import numpy as np

from ics_models.cue.kinematics import AXES, STATE_SIZE, Estimate, Matrix, Vector, seconds_between
from ics_models.endgame.motion import EndgameError, MotionModel, process_noise, transition

ROW_SUM_TOLERANCE: float = 1e-12
LOG_TWO_PI: float = math.log(2.0 * math.pi)
HALF: float = 0.5


@dataclass(frozen=True)
class ImmSettings:
    """The models, the switching matrix π and the chances the models start with."""

    models: tuple[MotionModel, ...]
    switching: tuple[tuple[float, ...], ...]
    initial: tuple[float, ...]

    def __post_init__(self) -> None:
        """Refuse a matrix or a start that is not square to the models, or not chances that sum to 1."""
        size = len(self.models)
        rows = (*self.switching, self.initial)
        shaped = size > 0 and len(self.switching) == size and all(len(row) == size for row in rows)
        chances = all(math.isfinite(value) and value >= 0.0 for row in rows for value in row)
        summed = all(abs(math.fsum(row) - 1.0) <= ROW_SUM_TOLERANCE for row in rows)
        if not (shaped and chances and summed):
            message = f"{size} models with switching {self.switching} and start {self.initial} cannot be used"
            raise EndgameError(message)


@dataclass(frozen=True)
class Measurement:
    """A position in the range ENU frame (m) at a UTC time in nanoseconds, with its covariance (m²)."""

    time_ns: int
    position: Vector
    covariance: Matrix


@dataclass(frozen=True)
class ImmState:
    """Each model's estimate, all at one time, and the chance of each model."""

    estimates: tuple[Estimate, ...]
    probabilities: Vector

    @property
    def time_ns(self) -> int:
        """The estimates' time."""
        return self.estimates[0].time_ns


def start(seed: Estimate, settings: ImmSettings) -> ImmState:
    """Every model starting from the seed, with the settings' initial chances."""
    if seed.state.shape != (STATE_SIZE,) or seed.covariance.shape != (STATE_SIZE, STATE_SIZE):
        message = f"a seed needs {STATE_SIZE} states and a {STATE_SIZE} by {STATE_SIZE} covariance"
        raise EndgameError(message)
    estimates = tuple(Estimate(seed.time_ns, seed.state.copy(), seed.covariance.copy()) for _ in settings.models)
    return ImmState(estimates, np.array(settings.initial, dtype=np.float64))


def mixture(estimates: Sequence[Estimate], weights: Vector) -> tuple[Vector, Matrix]:
    """The mean and covariance of estimates weighted by weights, which sum to 1."""
    mean = sum(
        (weight * estimate.state for weight, estimate in zip(weights, estimates, strict=True)), np.zeros(STATE_SIZE)
    )
    covariance = np.zeros((STATE_SIZE, STATE_SIZE))
    for weight, estimate in zip(weights, estimates, strict=True):
        spread = estimate.state - mean
        covariance += weight * (estimate.covariance + np.outer(spread, spread))
    return mean, covariance


def predict(state: ImmState, time_ns: int, settings: ImmSettings) -> ImmState:
    """Interaction and prediction to time_ns; the chances become the c_j."""
    if time_ns < state.time_ns:
        message = f"cannot predict back from {state.time_ns} ns to {time_ns} ns"
        raise EndgameError(message)
    switching = np.array(settings.switching, dtype=np.float64)
    weighted = switching * state.probabilities[:, np.newaxis]
    after = weighted.sum(axis=0)
    dt_s = seconds_between(state.time_ns, time_ns)
    predicted: list[Estimate] = []
    for column, model in enumerate(settings.models):
        if after[column] > 0.0:
            mean, covariance = mixture(state.estimates, weighted[:, column] / after[column])
        else:
            mean, covariance = state.estimates[column].state, state.estimates[column].covariance
        move = transition(model, dt_s)
        predicted.append(Estimate(time_ns, move @ mean, move @ covariance @ move.T + process_noise(model, dt_s)))
    return ImmState(tuple(predicted), after)


def filtered(predicted: Estimate, measurement: Measurement) -> tuple[Estimate, float]:
    """One model's estimate after the measurement, and the log-likelihood of its innovation."""
    observe = np.zeros((AXES, STATE_SIZE))
    observe[:, 0:AXES] = np.eye(AXES)
    innovation = measurement.position - observe @ predicted.state
    spread = observe @ predicted.covariance @ observe.T + measurement.covariance
    gain = np.linalg.solve(spread, observe @ predicted.covariance).T
    keep = np.eye(STATE_SIZE) - gain @ observe
    covariance = keep @ predicted.covariance @ keep.T + gain @ measurement.covariance @ gain.T
    sign, log_determinant = np.linalg.slogdet(spread)
    if sign <= 0.0:
        message = f"the innovation covariance at {measurement.time_ns} ns is not positive definite"
        raise EndgameError(message)
    distance = float(innovation @ np.linalg.solve(spread, innovation))
    log_likelihood = -HALF * (distance + float(log_determinant) + AXES * LOG_TWO_PI)
    return Estimate(predicted.time_ns, predicted.state + gain @ innovation, covariance), log_likelihood


def update(state: ImmState, measurement: Measurement, settings: ImmSettings) -> ImmState:
    """The state after one measurement, which may not be earlier than the state."""
    if measurement.position.shape != (AXES,) or measurement.covariance.shape != (AXES, AXES):
        message = f"a measurement needs {AXES} coordinates and a {AXES} by {AXES} covariance"
        raise EndgameError(message)
    predicted = predict(state, measurement.time_ns, settings)
    results = [filtered(estimate, measurement) for estimate in predicted.estimates]
    logs = np.array([log_likelihood for _, log_likelihood in results])
    scores = predicted.probabilities * np.exp(logs - logs.max())
    total = float(scores.sum())
    if not total > 0.0:
        message = f"no model can explain the measurement at {measurement.time_ns} ns"
        raise EndgameError(message)
    return ImmState(tuple(estimate for estimate, _ in results), scores / total)


def combined(state: ImmState) -> Estimate:
    """The mixture of the models' estimates, weighted by their chances."""
    mean, covariance = mixture(state.estimates, state.probabilities)
    return Estimate(state.time_ns, mean, covariance)
