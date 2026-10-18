"""The cue's Kalman filter: fuses position reports, gates outliers, re-filters late ones (ICS-037).

Each report is a position in the range ENU frame with one-sigma horizontal and
vertical errors, and a velocity when the source gives one (GNSS PLI does,
SAPIENT does not). The filter keeps the reports of the last few seconds, in
time order, each with the estimate after it: its checkpoint. A report older
than the newest is put in its place, and every report from there on is applied
again from the checkpoint before it, so the result is the same as if the
reports had come in order. A report whose position is too far from the
prediction, by the chi-squared distance with three degrees of freedom, is
gated: kept in the history, but not applied.
"""

import bisect
from dataclasses import dataclass, replace
from enum import StrEnum

import numpy as np

from ics_models.cue.kinematics import AXES, STATE_SIZE, Estimate, Matrix, ProcessNoise, Vector, predict


class CueError(ValueError):
    """An estimate or a cue that cannot be given, and why."""


class Outcome(StrEnum):
    """What the filter did with a report."""

    TAKEN = "taken"
    LATE = "late"
    GATED = "gated"
    TOO_OLD = "too_old"


@dataclass(frozen=True)
class Measurement:
    """A position report in the range ENU frame, at a UTC time in nanoseconds."""

    time_ns: int
    position: tuple[float, float, float]
    horizontal_sigma_m: float
    vertical_sigma_m: float
    velocity: tuple[float, float, float] | None
    velocity_sigma_m_s: float


@dataclass(frozen=True)
class FilterSettings:
    """How the filter weighs, gates and keeps reports."""

    noise: ProcessNoise
    gate: float
    history_ns: int
    coast_limit_ns: int
    capacity: int
    initial_velocity_sigma_m_s: float
    initial_acceleration_sigma_m_s2: float


@dataclass(frozen=True)
class Entry:
    """A report in the history: its arrival order, whether it was applied, and the estimate after it."""

    measurement: Measurement
    sequence: int
    applied: bool
    checkpoint: Estimate

    def key(self) -> tuple[int, int]:
        """The history's order: by time, then by arrival."""
        return (self.measurement.time_ns, self.sequence)


def measured_indices(measurement: Measurement) -> list[int]:
    """The state indices a report measures: position, and velocity when it has one."""
    count = 2 * AXES if measurement.velocity is not None else AXES
    return list(range(count))


def observation(measurement: Measurement) -> tuple[Vector, Matrix]:
    """A report's measured values and their covariance, in measured_indices's order."""
    sigmas = [measurement.horizontal_sigma_m, measurement.horizontal_sigma_m, measurement.vertical_sigma_m]
    values = list(measurement.position)
    if measurement.velocity is not None:
        values.extend(measurement.velocity)
        sigmas.extend([measurement.velocity_sigma_m_s] * AXES)
    return np.array(values), np.diag(np.square(np.array(sigmas)))


def initial_estimate(measurement: Measurement, settings: FilterSettings) -> Estimate:
    """The estimate a first report gives: its position and velocity, and no acceleration."""
    state = np.zeros(STATE_SIZE)
    state[0:AXES] = measurement.position
    velocity_sigma = settings.initial_velocity_sigma_m_s
    if measurement.velocity is not None:
        state[AXES : 2 * AXES] = measurement.velocity
        velocity_sigma = measurement.velocity_sigma_m_s
    horizontal = measurement.horizontal_sigma_m**2
    sigmas_squared = [horizontal, horizontal, measurement.vertical_sigma_m**2]
    sigmas_squared.extend([velocity_sigma**2] * AXES)
    sigmas_squared.extend([settings.initial_acceleration_sigma_m_s2**2] * AXES)
    return Estimate(measurement.time_ns, state, np.diag(sigmas_squared))


def gate_distance(predicted: Estimate, measurement: Measurement) -> float:
    """The squared Mahalanobis distance of a report's position from the prediction."""
    values, noise = observation(measurement)
    innovation = values[0:AXES] - predicted.state[0:AXES]
    spread = predicted.covariance[0:AXES, 0:AXES] + noise[0:AXES, 0:AXES]
    return float(innovation @ np.linalg.solve(spread, innovation))


def update(predicted: Estimate, measurement: Measurement) -> Estimate:
    """The estimate after applying a report to the prediction at its time, in Joseph form."""
    indices = measured_indices(measurement)
    values, noise = observation(measurement)
    covariance = predicted.covariance
    innovation = values - predicted.state[indices]
    spread = covariance[np.ix_(indices, indices)] + noise
    gain = np.linalg.solve(spread, covariance[indices, :]).T
    observe = np.zeros((len(indices), STATE_SIZE))
    observe[range(len(indices)), indices] = 1.0
    keep = np.eye(STATE_SIZE) - gain @ observe
    updated = keep @ covariance @ keep.T + gain @ noise @ gain.T
    return Estimate(predicted.time_ns, predicted.state + gain @ innovation, (updated + updated.T) / 2.0)


def step(prior: Estimate, measurement: Measurement, settings: FilterSettings) -> tuple[bool, Estimate]:
    """Whether a report passes the gate, and the checkpoint after it."""
    predicted = predict(prior, measurement.time_ns, settings.noise)
    if gate_distance(predicted, measurement) > settings.gate:
        return False, prior
    return True, update(predicted, measurement)


class CueFilter:
    """The filter for one target, with its history of reports."""

    def __init__(self, settings: FilterSettings) -> None:
        """An empty filter."""
        self._settings = settings
        self._entries: list[Entry] = []
        self._base: Estimate | None = None
        self._sequence = 0

    def add(self, measurement: Measurement) -> Outcome:
        """Applies a report, in its time order, and says what became of it."""
        newest = self._entries[-1].measurement.time_ns if self._entries else measurement.time_ns
        too_old = measurement.time_ns < newest - self._settings.history_ns
        if too_old or (self._base is not None and measurement.time_ns < self._base.time_ns):
            return Outcome.TOO_OLD
        entry = Entry(
            measurement, self._sequence, applied=True, checkpoint=initial_estimate(measurement, self._settings)
        )
        self._sequence += 1
        position = bisect.bisect_right([held.key() for held in self._entries], entry.key())
        self._entries.insert(position, entry)
        self._refilter(position)
        applied = self._entries[position].applied
        last = position == len(self._entries) - 1
        self._evict()
        if not applied:
            return Outcome.GATED
        return Outcome.TAKEN if last else Outcome.LATE

    def latest_ns(self) -> int | None:
        """The time of the newest report held, or None before the first."""
        return self._entries[-1].measurement.time_ns if self._entries else None

    def estimate_at(self, time_ns: int) -> Estimate:
        """The estimate predicted to a time no earlier than the history, within the coast limit."""
        checkpoint = self._checkpoint_at(time_ns)
        if time_ns - checkpoint.time_ns > self._settings.coast_limit_ns:
            message = f"{time_ns} ns is past the coast limit after the report at {checkpoint.time_ns} ns"
            raise CueError(message)
        return predict(checkpoint, time_ns, self._settings.noise)

    def _checkpoint_at(self, time_ns: int) -> Estimate:
        """The checkpoint of the latest report at or before a time."""
        times = [held.measurement.time_ns for held in self._entries]
        position = bisect.bisect_right(times, time_ns)
        if position > 0:
            return self._entries[position - 1].checkpoint
        if self._base is not None and self._base.time_ns <= time_ns:
            return self._base
        message = f"no estimate at {time_ns} ns: no report at or before it is held"
        raise CueError(message)

    def _refilter(self, start: int) -> None:
        """Applies every report from start on again, from the checkpoint before it."""
        for position in range(start, len(self._entries)):
            entry = self._entries[position]
            prior = self._entries[position - 1].checkpoint if position > 0 else self._base
            if prior is None:
                checkpoint = initial_estimate(entry.measurement, self._settings)
                self._entries[position] = replace(entry, applied=True, checkpoint=checkpoint)
            else:
                applied, checkpoint = step(prior, entry.measurement, self._settings)
                self._entries[position] = replace(entry, applied=applied, checkpoint=checkpoint)

    def _evict(self) -> None:
        """Drops the reports past the history's length or capacity; the last dropped one's checkpoint is the base."""
        newest = self._entries[-1].measurement.time_ns
        while len(self._entries) > 1 and (
            len(self._entries) > self._settings.capacity
            or self._entries[0].measurement.time_ns < newest - self._settings.history_ns
        ):
            self._base = self._entries.pop(0).checkpoint
