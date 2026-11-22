"""Closest approach, miss distance and contact time of two estimates (ICS-046).

The relative motion keeps the acceleration it has now: the interceptor's
position from the target is s(t) = p + v·t + a·t²/2, for p, v and a the
interceptor's less the target's. |s|² falls while f(t) = s·s' is negative,
and f is the cubic

    f(t) = (a·a/2)·t³ + (3/2)(v·a)·t² + (v·v + p·a)·t + p·v.

The closest approach is the first time ahead, inside the horizon, at which f
turns from negative to positive. Between the roots of f' (a quadratic), f is
monotonic, so each sign change is found by a fixed number of bisections:
there is no tolerance to tune, and the C++ port takes the same steps.

The miss distance's sigma is first order. At the closest approach its gradient
with respect to the relative state is u·[I, t·I, t²/2·I], for u the unit
separation (the time's own sensitivity vanishes there), and the relative
covariance is the sum of the two, which are independent. With no separation
at all, u is undefined, and sigma is the root mean square over the three axes.

Contact is the first time the separation is no more than the contact radius,
on the way in to the closest approach; it is now if it already is.
"""

import itertools
import math
from dataclasses import dataclass

import numpy as np

from ics_models.cue.kinematics import AXES, NANOSECONDS_PER_SECOND, STATE_SIZE, Estimate, Matrix, Vector
from ics_models.endgame.motion import EndgameError

BISECTIONS: int = 64
HALF: float = 0.5
THREE_HALVES: float = 1.5


@dataclass(frozen=True)
class EncounterSettings:
    """How far ahead to look, in nanoseconds, and the separation that counts as contact, in metres."""

    horizon_ns: int
    contact_radius_m: float

    def __post_init__(self) -> None:
        """Refuse a horizon that is not positive and a radius that is negative or not finite."""
        radius_valid = math.isfinite(self.contact_radius_m) and self.contact_radius_m >= 0.0
        if self.horizon_ns <= 0 or not radius_valid:
            message = f"a {self.horizon_ns} ns horizon and a {self.contact_radius_m} m contact radius cannot be used"
            raise EndgameError(message)


@dataclass(frozen=True)
class Encounter:
    """A closest approach: UTC times in nanoseconds, and the miss distance and its sigma in metres."""

    time_ns: int
    miss_m: float
    miss_sigma_m: float
    contact_ns: int | None


@dataclass(frozen=True)
class Relative:
    """The interceptor's position, velocity and acceleration from the target."""

    position: Vector
    velocity: Vector
    acceleration: Vector

    def separation(self, seconds: float) -> Vector:
        """s(t)."""
        return self.position + self.velocity * seconds + self.acceleration * (HALF * seconds * seconds)

    def closing(self, seconds: float) -> float:
        """f(t) = s(t)·s'(t)."""
        return float(self.separation(seconds) @ (self.velocity + self.acceleration * seconds))


def relative_of(target: Estimate, interceptor: Estimate) -> Relative:
    """The relative motion of two estimates at one time."""
    if target.time_ns != interceptor.time_ns:
        message = f"the estimates are at {target.time_ns} ns and {interceptor.time_ns} ns, not one time"
        raise EndgameError(message)
    difference = interceptor.state - target.state
    return Relative(difference[0:AXES], difference[AXES : 2 * AXES], difference[2 * AXES :])


def turning_points(relative: Relative, horizon_s: float) -> list[float]:
    """0, the roots of f' strictly inside the horizon in order, and the horizon.

    With no relative acceleration f' is the constant v·v, so f has no turning point.
    """
    quadratic = THREE_HALVES * float(relative.acceleration @ relative.acceleration)
    linear = 3.0 * float(relative.velocity @ relative.acceleration)
    constant = float(relative.velocity @ relative.velocity + relative.position @ relative.acceleration)
    roots: list[float] = []
    if quadratic > 0.0:
        discriminant = linear * linear - 4.0 * quadratic * constant
        if discriminant > 0.0:
            root = math.sqrt(discriminant)
            # The numerically stable pair: q = -(b + sign(b)·√d)/2, never 0 when d > 0; roots q/a and c/q.
            q = -HALF * (linear + math.copysign(root, linear))
            roots = sorted((q / quadratic, constant / q))
    inside = [root for root in roots if 0.0 < root < horizon_s]
    return [0.0, *inside, horizon_s]


def bisect_rising(relative: Relative, low: float, high: float) -> float:
    """The time in [low, high] at which f, negative at low and not at high, turns."""
    for _ in range(BISECTIONS):
        middle = HALF * (low + high)
        if relative.closing(middle) < 0.0:
            low = middle
        else:
            high = middle
    return high


def bisect_inside(relative: Relative, radius_m: float, low: float, high: float) -> float:
    """The time in [low, high] at which |s|, above radius_m at low and not at high, falls to it."""
    radius_squared = radius_m * radius_m
    for _ in range(BISECTIONS):
        middle = HALF * (low + high)
        separation = relative.separation(middle)
        if float(separation @ separation) > radius_squared:
            low = middle
        else:
            high = middle
    return high


def first_minimum(relative: Relative, horizon_s: float) -> tuple[float, float] | None:
    """The first closest approach inside the horizon, and the start of the fall to it; None if there is none."""
    points = turning_points(relative, horizon_s)
    falling_from = 0.0
    for low, high in itertools.pairwise(points):
        at_low, at_high = relative.closing(low), relative.closing(high)
        if at_low < 0.0 <= at_high:
            return bisect_rising(relative, low, high), falling_from
        if at_low >= 0.0 > at_high:
            falling_from = low
    return None


def gradient(relative: Relative, seconds: float) -> Matrix:
    """The miss distance's gradient by the relative state, or rows per axis when there is no separation."""
    separation = relative.separation(seconds)
    miss = math.sqrt(float(separation @ separation))
    directions = np.eye(AXES) / math.sqrt(AXES) if miss == 0.0 else (separation / miss).reshape(1, AXES)
    rows = np.zeros((directions.shape[0], STATE_SIZE))
    rows[:, 0:AXES] = directions
    rows[:, AXES : 2 * AXES] = directions * seconds
    rows[:, 2 * AXES :] = directions * (HALF * seconds * seconds)
    return rows


def miss_sigma(relative: Relative, seconds: float, covariance: Matrix) -> float:
    """The miss distance's first-order sigma."""
    rows = gradient(relative, seconds)
    return math.sqrt(max(0.0, float(np.trace(rows @ covariance @ rows.T))))


def at_ns(time_ns: int, seconds: float) -> int:
    """A time seconds after time_ns, to the nearest nanosecond."""
    return time_ns + math.floor(seconds * NANOSECONDS_PER_SECOND + HALF)


def encounter(target: Estimate, interceptor: Estimate, settings: EncounterSettings) -> Encounter | None:
    """The first closest approach ahead of two estimates at one time, or None when there is none inside the horizon."""
    relative = relative_of(target, interceptor)
    found = first_minimum(relative, float(settings.horizon_ns) / NANOSECONDS_PER_SECOND)
    if found is None:
        return None
    seconds, falling_from = found
    separation = relative.separation(seconds)
    miss = math.sqrt(float(separation @ separation))
    sigma = miss_sigma(relative, seconds, target.covariance + interceptor.covariance)
    radius = settings.contact_radius_m
    contact: int | None = None
    if float(relative.position @ relative.position) <= radius * radius:
        contact = target.time_ns
    elif miss <= radius:
        # Before falling_from the separation only grew, so it is outside the radius there.
        contact = at_ns(target.time_ns, bisect_inside(relative, radius, falling_from, seconds))
    return Encounter(at_ns(target.time_ns, seconds), miss, sigma, contact)
