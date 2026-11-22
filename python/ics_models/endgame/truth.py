"""Synthetic truth for the endgame references (ICS-046).

A body flies a chain of legs, each for a whole number of nanoseconds: a
constant acceleration (zero for a straight leg), or a horizontal turn at a
fixed rate with constant velocity up. Each leg is evaluated in closed form, so
the truth is exact and owes nothing to the filters' transitions.
"""

import math
from collections.abc import Sequence
from dataclasses import dataclass

import numpy as np

from ics_models.cue.kinematics import AXES, NANOSECONDS_PER_SECOND, STATE_SIZE, Vector, seconds_between
from ics_models.endgame.motion import EAST, NORTH, EndgameError


@dataclass(frozen=True)
class Leg:
    """A leg: its length, and either its acceleration (m/s², ENU) or its turn rate (rad/s)."""

    duration_ns: int
    acceleration: tuple[float, float, float] = (0.0, 0.0, 0.0)
    turn_rate_rad_s: float = 0.0


def accelerated(start: Vector, acceleration: Vector, dt_s: float) -> Vector:
    """A state dt_s after start on a constant acceleration."""
    position, velocity = start[0:AXES], start[AXES : 2 * AXES]
    state = np.zeros(STATE_SIZE)
    state[0:AXES] = position + velocity * dt_s + acceleration * (dt_s * dt_s / 2.0)
    state[AXES : 2 * AXES] = velocity + acceleration * dt_s
    state[2 * AXES :] = acceleration
    return state


def turned(start: Vector, rate_rad_s: float, dt_s: float) -> Vector:
    """A state dt_s after start, turning horizontally at rate_rad_s."""
    east, north, up = (float(value) for value in start[AXES : 2 * AXES])
    sine, cosine = math.sin(rate_rad_s * dt_s), math.cos(rate_rad_s * dt_s)
    state = np.zeros(STATE_SIZE)
    state[EAST] = start[EAST] + (sine * east - (1.0 - cosine) * north) / rate_rad_s
    state[NORTH] = start[NORTH] + ((1.0 - cosine) * east + sine * north) / rate_rad_s
    state[AXES - 1] = start[AXES - 1] + up * dt_s
    state[AXES + EAST] = cosine * east - sine * north
    state[AXES + NORTH] = sine * east + cosine * north
    state[AXES + AXES - 1] = up
    state[2 * AXES + EAST] = -rate_rad_s * state[AXES + NORTH]
    state[2 * AXES + NORTH] = rate_rad_s * state[AXES + EAST]
    return state


def along_leg(start: Vector, leg: Leg, dt_s: float) -> Vector:
    """A state dt_s into a leg that begins at start."""
    if leg.turn_rate_rad_s != 0.0:
        return turned(start, leg.turn_rate_rad_s, dt_s)
    return accelerated(start, np.array(leg.acceleration), dt_s)


def leg_starts(start: Vector, start_ns: int, legs: Sequence[Leg]) -> list[tuple[int, Vector]]:
    """Each leg's start time and state."""
    if any(leg.duration_ns <= 0 for leg in legs) or not legs:
        message = "a flight needs legs, each of positive length"
        raise EndgameError(message)
    starts: list[tuple[int, Vector]] = []
    time_ns, state = start_ns, start.astype(np.float64)
    for leg in legs:
        state = along_leg(state, leg, 0.0)
        starts.append((time_ns, state))
        state = along_leg(state, leg, float(leg.duration_ns) / NANOSECONDS_PER_SECOND)
        time_ns += leg.duration_ns
    return starts


def flight(start: Vector, start_ns: int, legs: Sequence[Leg], times_ns: Sequence[int]) -> list[Vector]:
    """The true state at each time, from a position and velocity at start_ns, along the legs.

    A time past the last leg continues it.
    """
    starts = leg_starts(start, start_ns, legs)
    states: list[Vector] = []
    for time_ns in times_ns:
        if time_ns < start_ns:
            message = f"time {time_ns} ns precedes the flight's start at {start_ns} ns"
            raise EndgameError(message)
        current = max(number for number, (begin_ns, _) in enumerate(starts) if begin_ns <= time_ns)
        begin_ns, state = starts[current]
        states.append(along_leg(state, legs[current], seconds_between(begin_ns, time_ns)))
    return states
