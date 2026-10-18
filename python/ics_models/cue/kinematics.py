"""The cue's constant-acceleration state and its prediction (ICS-037).

The state is nine numbers in the range ENU frame: position east, north and up
(m), then velocity (m/s), then acceleration (m/s²). Each axis follows the same
model on its own: acceleration driven by white jerk, whose power spectral
density (m²/s⁵) is one value for east and north and another for up. The
discrete model is exact for that continuous one, so predicting over dt1 then
dt2 equals predicting over dt1 + dt2.
"""

from dataclasses import dataclass

import numpy as np
import numpy.typing as npt

type Vector = npt.NDArray[np.float64]
type Matrix = npt.NDArray[np.float64]

AXES: int = 3
ORDERS: int = 3
STATE_SIZE: int = AXES * ORDERS
NANOSECONDS_PER_SECOND: float = 1e9
# The terms of the white-jerk process noise for one axis, as divisors of
# dt^5, dt^4, dt^3, dt^2 and dt.
DT5_DIVISOR: float = 20.0
DT4_DIVISOR: float = 8.0
DT3_DIVISOR: float = 6.0
DT3_VELOCITY_DIVISOR: float = 3.0
DT2_DIVISOR: float = 2.0


@dataclass(frozen=True)
class ProcessNoise:
    """The white-jerk power spectral density of each axis, in m²/s⁵."""

    horizontal: float
    vertical: float

    def axis(self, axis: int) -> float:
        """The density for an axis: 0 east, 1 north, 2 up."""
        return self.vertical if axis == AXES - 1 else self.horizontal


@dataclass(frozen=True)
class Estimate:
    """The state and its covariance at a UTC time, in nanoseconds."""

    time_ns: int
    state: Vector
    covariance: Matrix


def index(order: int, axis: int) -> int:
    """The state index of an order (0 position, 1 velocity, 2 acceleration) on an axis."""
    return order * AXES + axis


def seconds_between(earlier_ns: int, later_ns: int) -> float:
    """later - earlier, in seconds, computed as the C++ port does."""
    return float(later_ns - earlier_ns) / NANOSECONDS_PER_SECOND


def transition(dt_s: float) -> Matrix:
    """The state transition over dt_s seconds."""
    matrix = np.eye(STATE_SIZE)
    for axis in range(AXES):
        matrix[index(0, axis), index(1, axis)] = dt_s
        matrix[index(0, axis), index(2, axis)] = dt_s * dt_s / DT2_DIVISOR
        matrix[index(1, axis), index(2, axis)] = dt_s
    return matrix


def process_noise(dt_s: float, noise: ProcessNoise) -> Matrix:
    """The process noise added over dt_s seconds."""
    dt2 = dt_s * dt_s
    dt3 = dt2 * dt_s
    block = (
        (dt3 * dt2 / DT5_DIVISOR, dt2 * dt2 / DT4_DIVISOR, dt3 / DT3_DIVISOR),
        (dt2 * dt2 / DT4_DIVISOR, dt3 / DT3_VELOCITY_DIVISOR, dt2 / DT2_DIVISOR),
        (dt3 / DT3_DIVISOR, dt2 / DT2_DIVISOR, dt_s),
    )
    matrix = np.zeros((STATE_SIZE, STATE_SIZE))
    for axis in range(AXES):
        density = noise.axis(axis)
        for row in range(ORDERS):
            for column in range(ORDERS):
                matrix[index(row, axis), index(column, axis)] = density * block[row][column]
    return matrix


def predict(estimate: Estimate, time_ns: int, noise: ProcessNoise) -> Estimate:
    """The estimate predicted to time_ns, which may not be earlier than the estimate's time."""
    if time_ns < estimate.time_ns:
        message = f"cannot predict back from {estimate.time_ns} ns to {time_ns} ns"
        raise ValueError(message)
    dt_s = seconds_between(estimate.time_ns, time_ns)
    move = transition(dt_s)
    covariance = move @ estimate.covariance @ move.T + process_noise(dt_s, noise)
    return Estimate(time_ns, move @ estimate.state, covariance)
