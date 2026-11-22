"""The IMM's motion models, all in the cue's nine-number state (ICS-046).

Every model carries position, velocity and acceleration in the range ENU
frame, in that order, so the IMM mixes them without changing dimension:

- constant velocity: velocity driven by white acceleration; the acceleration
  is set to 0;
- constant acceleration: acceleration driven by white jerk, exactly as the cue
  filter (ICS-037);
- coordinated turn: the horizontal velocity turns at a fixed rate ω, positive
  anticlockwise seen from above, and the up axis keeps constant velocity; the
  acceleration is set to the centripetal ω cross v. Each axis's velocity is driven
  by white acceleration, as in the constant-velocity model.

The noise densities are m²/s³ for white acceleration and m²/s⁵ for white jerk.
"""

import math
from dataclasses import dataclass
from enum import StrEnum

import numpy as np

from ics_models.cue.kinematics import AXES, STATE_SIZE, Matrix, ProcessNoise, index
from ics_models.cue.kinematics import process_noise as jerk_noise
from ics_models.cue.kinematics import transition as jerk_transition

EAST: int = 0
NORTH: int = 1
UP: int = 2
VELOCITY_DT3_DIVISOR: float = 3.0
VELOCITY_DT2_DIVISOR: float = 2.0
SMALLEST_TURN_RATE_RAD_S: float = 1e-6


class EndgameError(ValueError):
    """An input the endgame references cannot use."""


class Kind(StrEnum):
    """A motion model's kind."""

    CONSTANT_VELOCITY = "cv"
    CONSTANT_ACCELERATION = "ca"
    TURN = "turn"


@dataclass(frozen=True)
class MotionModel:
    """One model: its kind, its noise densities, and for a turn its rate in rad/s."""

    kind: Kind
    horizontal: float
    vertical: float
    turn_rate_rad_s: float = 0.0

    def __post_init__(self) -> None:
        """Refuse negative or non-finite densities, and a turn with no rate or another model with one."""
        densities = (self.horizontal, self.vertical)
        valid = all(math.isfinite(value) and value >= 0.0 for value in densities)
        turning = abs(self.turn_rate_rad_s) >= SMALLEST_TURN_RATE_RAD_S
        valid = valid and math.isfinite(self.turn_rate_rad_s) and turning == (self.kind == Kind.TURN)
        if not valid:
            message = f"{self.kind} with densities {densities} and turn rate {self.turn_rate_rad_s} cannot be used"
            raise EndgameError(message)


def constant_velocity_transition(dt_s: float) -> Matrix:
    """Position moves with velocity; the acceleration becomes 0."""
    matrix = np.zeros((STATE_SIZE, STATE_SIZE))
    for axis in range(AXES):
        matrix[index(0, axis), index(0, axis)] = 1.0
        matrix[index(0, axis), index(1, axis)] = dt_s
        matrix[index(1, axis), index(1, axis)] = 1.0
    return matrix


def turn_transition(dt_s: float, rate_rad_s: float) -> Matrix:
    """A horizontal turn at rate_rad_s, constant velocity up, and the centripetal acceleration."""
    matrix = constant_velocity_transition(dt_s)
    sine, cosine = math.sin(rate_rad_s * dt_s), math.cos(rate_rad_s * dt_s)
    along, across = sine / rate_rad_s, (1.0 - cosine) / rate_rad_s
    rotation = ((cosine, -sine), (sine, cosine))
    swept = ((along, -across), (across, along))
    for row in (EAST, NORTH):
        for column in (EAST, NORTH):
            matrix[index(0, row), index(1, column)] = swept[row][column]
            matrix[index(1, row), index(1, column)] = rotation[row][column]
    for column in (EAST, NORTH):
        matrix[index(2, EAST), index(1, column)] = -rate_rad_s * rotation[NORTH][column]
        matrix[index(2, NORTH), index(1, column)] = rate_rad_s * rotation[EAST][column]
    return matrix


def transition(model: MotionModel, dt_s: float) -> Matrix:
    """The model's state transition over dt_s seconds."""
    if model.kind == Kind.CONSTANT_ACCELERATION:
        return jerk_transition(dt_s)
    if model.kind == Kind.TURN:
        return turn_transition(dt_s, model.turn_rate_rad_s)
    return constant_velocity_transition(dt_s)


def acceleration_noise(dt_s: float, horizontal: float, vertical: float) -> Matrix:
    """White acceleration's noise on position and velocity, and none on the acceleration."""
    dt2 = dt_s * dt_s
    block = ((dt2 * dt_s / VELOCITY_DT3_DIVISOR, dt2 / VELOCITY_DT2_DIVISOR), (dt2 / VELOCITY_DT2_DIVISOR, dt_s))
    matrix = np.zeros((STATE_SIZE, STATE_SIZE))
    for axis in range(AXES):
        density = vertical if axis == UP else horizontal
        for row in range(2):
            for column in range(2):
                matrix[index(row, axis), index(column, axis)] = density * block[row][column]
    return matrix


def process_noise(model: MotionModel, dt_s: float) -> Matrix:
    """The noise the model adds over dt_s seconds."""
    if model.kind == Kind.CONSTANT_ACCELERATION:
        return jerk_noise(dt_s, ProcessNoise(model.horizontal, model.vertical))
    return acceleration_noise(dt_s, model.horizontal, model.vertical)
