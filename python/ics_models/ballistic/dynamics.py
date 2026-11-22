"""The point-mass drag model and its propagation (ICS-044).

The acceleration is a = -g·up - ½·rho(h)·κ·|v|·v, where h is the range origin's
height above mean sea level plus the piece's up coordinate. κ is constant.
Gravity is flat-Earth and constant, and there is no Coriolis term and no
wind: debris falls for seconds over a few hundred metres.

Propagation is classic fourth-order Runge-Kutta in whole-nanosecond steps of
at most step_ns, ending exactly on each time asked for, so the C++ port takes
the same steps. Several states can be carried at once, as rows of a matrix:
the fit propagates its perturbed states together.
"""

import math
from collections.abc import Callable, Sequence
from dataclasses import dataclass

import numpy as np
import numpy.typing as npt

from ics_models.ballistic.atmosphere import STANDARD_GRAVITY_M_S2, density

type Vector = npt.NDArray[np.float64]
type Matrix = npt.NDArray[np.float64]

STATE_SIZE: int = 7
KAPPA: int = 6
UP: int = 2
NANOSECONDS_PER_SECOND: float = 1e9
HALF: float = 0.5
SIXTH: float = 1.0 / 6.0


@dataclass(frozen=True)
class Model:
    """The range's settings: its origin's height above mean sea level, gravity, and the RK4 step."""

    origin_height_m: float
    gravity_m_s2: float = STANDARD_GRAVITY_M_S2
    step_ns: int = 10_000_000


def derivative(states: Matrix, model: Model) -> Matrix:
    """The time derivative of each state row: velocity, acceleration, and 0 for κ."""
    rates = np.zeros_like(states)
    for row in range(states.shape[0]):
        east, north, up = float(states[row, 3]), float(states[row, 4]), float(states[row, 5])
        speed = math.sqrt(east * east + north * north + up * up)
        drag = HALF * density(model.origin_height_m + float(states[row, UP])) * float(states[row, KAPPA]) * speed
        rates[row, 0:3] = (east, north, up)
        rates[row, 3:6] = (-drag * east, -drag * north, -drag * up - model.gravity_m_s2)
    return rates


type Derivative = Callable[[Matrix, Model], Matrix]


@dataclass(frozen=True)
class Law:
    """A law of motion: the time derivative of state rows, and how many numbers each row holds."""

    rates: Derivative
    size: int


DRAG = Law(rates=derivative, size=STATE_SIZE)


def rk4_step(states: Matrix, seconds: float, model: Model, rates: Derivative = derivative) -> Matrix:
    """The states one RK4 step of the given length later, under a derivative (the drag model's by default)."""
    first = rates(states, model)
    second = rates(states + (HALF * seconds) * first, model)
    third = rates(states + (HALF * seconds) * second, model)
    fourth = rates(states + seconds * third, model)
    return states + (SIXTH * seconds) * (first + 2.0 * second + 2.0 * third + fourth)


def propagate(states: Matrix, start_ns: int, times_ns: Sequence[int], model: Model) -> npt.NDArray[np.float64]:
    """The states at each time, from rows given at start_ns: an array of times by rows by 7.

    The times must not decrease and none may precede start_ns.
    """
    return propagate_with(DRAG, states, start_ns, times_ns, model)


def propagate_with(
    law: Law, states: Matrix, start_ns: int, times_ns: Sequence[int], model: Model
) -> npt.NDArray[np.float64]:
    """propagate under any law of motion: an array of times by rows by the law's state size (ICS-045 uses it)."""
    size = law.size
    if states.ndim != UP or states.shape[1] != size:
        msg = f"states must be rows of {size} numbers"
        raise ValueError(msg)
    if model.step_ns <= 0:
        msg = "the RK4 step must be positive"
        raise ValueError(msg)
    result = np.empty((len(times_ns), states.shape[0], size))
    current_ns, current = start_ns, states.astype(np.float64)
    for index, time_ns in enumerate(times_ns):
        if time_ns < current_ns:
            msg = f"time {time_ns} ns precedes {current_ns} ns"
            raise ValueError(msg)
        while current_ns < time_ns:
            step_ns = min(model.step_ns, time_ns - current_ns)
            current = rk4_step(current, float(step_ns) / NANOSECONDS_PER_SECOND, model, law.rates)
            current_ns += step_ns
        result[index] = current
    return result


def trajectory(state: Vector, start_ns: int, times_ns: Sequence[int], model: Model) -> Matrix:
    """One state's trajectory: a row of 7 for each time."""
    return propagate(state.reshape(1, STATE_SIZE), start_ns, times_ns, model)[:, 0, :]
