"""Golden vectors for the IMM tracker and closest approach (ICS-046), which CPP-26 (ICS-067, ICS-068) must match.

Each CSV file has a header, and every number is printed to round-trip:

- parameters.csv: the models, the switching matrix, the start and the
  encounter settings the other files were written with;
- motion.csv: each model's transition and process noise for three step
  lengths, row by row;
- measurements.csv: the scripted engagement's measurements of both bodies,
  every 10 ms, each with MEASUREMENT_SIGMA_M of noise on each axis;
- tracks.csv: each body's model chances and combined state and covariance
  (upper triangle, row by row) after every tenth measurement and the last;
- encounters.csv: the encounter of the two combined estimates at the same
  times, with found = 0 when there is none inside the horizon;
- approaches.csv: encounter for random estimate pairs with diagonal
  covariances (given as sigmas), and for the edge cases: receding, parked, past the
  horizon, in contact already, a direct hit, and no relative acceleration.

In the scripted engagement, the target flies straight, turns left at
0.5 rad/s, then climbs while accelerating; the interceptor accelerates
steadily and passes TRUE_MISS_M from it, TRUE_CLOSEST_NS after the start.

python -m ics_golden.imm DIRECTORY writes them.
"""

import sys
from collections.abc import Iterable
from pathlib import Path

import numpy as np

from ics_models.cue.kinematics import AXES, STATE_SIZE, Estimate, Matrix, Vector
from ics_models.endgame.approach import Encounter, EncounterSettings, encounter
from ics_models.endgame.imm import ImmSettings, ImmState, Measurement, combined, start, update
from ics_models.endgame.motion import Kind, MotionModel, process_noise, transition
from ics_models.endgame.truth import Leg, flight

EXIT_USAGE: int = 2
SEED: int = 46
START_NS: int = 1_760_000_000_000_000_000
NS_PER_S: int = 1_000_000_000
MEASUREMENT_NS: int = 10_000_000
MEASUREMENTS: int = 500
WRITE_EVERY: int = 10
MEASUREMENT_SIGMA_M: float = 0.005
TURN_RATE_RAD_S: float = 0.5
STAY: float = 0.97
MODELS: tuple[MotionModel, ...] = (
    MotionModel(Kind.CONSTANT_VELOCITY, 0.05, 0.05),
    MotionModel(Kind.CONSTANT_ACCELERATION, 10.0, 10.0),
    MotionModel(Kind.TURN, 0.05, 0.05, TURN_RATE_RAD_S),
    MotionModel(Kind.TURN, 0.05, 0.05, -TURN_RATE_RAD_S),
)
SWITCH: float = (1.0 - STAY) / (len(MODELS) - 1)
SETTINGS = ImmSettings(
    MODELS,
    tuple(tuple(STAY if row == column else SWITCH for column in range(len(MODELS))) for row in range(len(MODELS))),
    tuple(1.0 / len(MODELS) for _ in MODELS),
)
ENCOUNTER = EncounterSettings(horizon_ns=2 * NS_PER_S, contact_radius_m=0.5)
SEED_SIGMAS: tuple[float, ...] = (1.0, 1.0, 1.0, 5.0, 5.0, 5.0, 3.0, 3.0, 3.0)
TARGET_START: tuple[float, ...] = (0.0, 0.0, 100.0, 20.0, 0.0, 0.0, 0.0, 0.0, 0.0)
TARGET_LEGS: tuple[Leg, ...] = (
    Leg(2 * NS_PER_S),
    Leg(2 * NS_PER_S, turn_rate_rad_s=TURN_RATE_RAD_S),
    Leg(2 * NS_PER_S, acceleration=(3.0, -2.0, 1.0)),
)
INTERCEPTOR_VELOCITY: tuple[float, float, float] = (30.0, 40.0, 5.0)
INTERCEPTOR_ACCELERATION: tuple[float, float, float] = (2.0, -1.0, -1.0)
INTERCEPTOR_LEGS: tuple[Leg, ...] = (Leg(6 * NS_PER_S, acceleration=INTERCEPTOR_ACCELERATION),)
TRUE_CLOSEST_NS: int = 5 * NS_PER_S
TRUE_MISS_M: float = 0.3
DT_NS: tuple[int, ...] = (1_000_000, 10_000_000, 730_000_000)
RANDOM_APPROACHES: int = 40
BODIES: tuple[str, ...] = ("target", "interceptor")
STATE_NAMES: tuple[str, ...] = ("east", "north", "up", "v_east", "v_north", "v_up", "a_east", "a_north", "a_up")


def numbers(values: Iterable[float | int | str]) -> str:
    """Values joined by commas, each float printed to round-trip."""
    return ",".join(repr(float(value)) if isinstance(value, float) else str(value) for value in values)


def upper(matrix: Matrix) -> list[float]:
    """A square matrix's upper triangle, row by row."""
    size = matrix.shape[0]
    return [float(matrix[row, column]) for row in range(size) for column in range(row, size)]


def upper_names(prefix: str, size: int) -> list[str]:
    """The names of upper's entries."""
    return [f"{prefix}_{row}_{column}" for row in range(size) for column in range(row, size)]


def seed_covariance() -> Matrix:
    """The covariance each track starts with."""
    return np.diag(np.square(np.array(SEED_SIGMAS)))


def interceptor_start() -> Vector:
    """The interceptor's start: on its constant acceleration, it passes TRUE_MISS_M from the target.

    It passes on the side square to their relative velocity then and to east,
    so that is the closest approach, and TRUE_MISS_M the miss, exactly.
    """
    target = flight(np.array(TARGET_START), START_NS, TARGET_LEGS, [START_NS + TRUE_CLOSEST_NS])[0]
    seconds = float(TRUE_CLOSEST_NS) / NS_PER_S
    velocity, acceleration = np.array(INTERCEPTOR_VELOCITY), np.array(INTERCEPTOR_ACCELERATION)
    relative = velocity + acceleration * seconds - target[AXES : 2 * AXES]
    side = np.cross(relative, np.array([1.0, 0.0, 0.0]))
    offset = side * (TRUE_MISS_M / float(np.linalg.norm(side)))
    position = target[0:AXES] + offset - velocity * seconds - acceleration * (seconds * seconds / 2.0)
    return np.concatenate([position, velocity, np.zeros(AXES)])


def measurement_times() -> list[int]:
    """Every measurement time, after the start."""
    return [START_NS + number * MEASUREMENT_NS for number in range(1, MEASUREMENTS)]


def measured(rng: np.random.Generator, sigma_m: float, noisy: bool) -> tuple[list[Measurement], list[Measurement]]:
    """Both bodies' measurements with a covariance of sigma_m on each axis, and noise of it if noisy."""
    times = measurement_times()
    paths = (
        flight(np.array(TARGET_START), START_NS, TARGET_LEGS, times),
        flight(interceptor_start(), START_NS, INTERCEPTOR_LEGS, times),
    )
    covariance = np.eye(AXES) * (sigma_m * sigma_m)
    scale = sigma_m if noisy else 0.0
    target, interceptor = (
        [Measurement(t, s[0:AXES] + rng.normal(0.0, scale, AXES), covariance) for t, s in zip(times, path, strict=True)]
        for path in paths
    )
    return target, interceptor


def tracked(measurements: list[Measurement], start_state: Vector) -> list[ImmState]:
    """A body's IMM state after each measurement, from its true start and the seed covariance."""
    state = start(Estimate(START_NS, start_state, seed_covariance()), SETTINGS)
    states: list[ImmState] = []
    for measurement in measurements:
        state = update(state, measurement, SETTINGS)
        states.append(state)
    return states


def engagement(
    rng: np.random.Generator, sigma_m: float, noisy: bool = True
) -> tuple[list[Measurement], list[ImmState], list[ImmState]]:
    """The scripted engagement: the target's measurements then the interceptor's, and each track."""
    target, interceptor = measured(rng, sigma_m, noisy)
    measurements = [*target, *interceptor]
    return measurements, tracked(target, np.array(TARGET_START)), tracked(interceptor, interceptor_start())


def encounter_fields(found: Encounter | None) -> list[float | int | str]:
    """An encounter's fields: found, time, miss, sigma, and contact (or an empty field)."""
    if found is None:
        return [0, "", "", "", ""]
    contact: int | str = "" if found.contact_ns is None else found.contact_ns
    return [1, found.time_ns, found.miss_m, found.miss_sigma_m, contact]


def parameter_rows() -> str:
    """parameters.csv's text."""
    rows: list[tuple[str, float | int | str]] = []
    for number, model in enumerate(MODELS):
        rows.extend(
            (
                (f"model_{number}_kind", str(model.kind)),
                (f"model_{number}_horizontal", model.horizontal),
                (f"model_{number}_vertical", model.vertical),
                (f"model_{number}_turn_rate_rad_s", model.turn_rate_rad_s),
                (f"initial_{number}", SETTINGS.initial[number]),
            )
        )
        rows.extend((f"switching_{number}_{column}", value) for column, value in enumerate(SETTINGS.switching[number]))
    rows.extend((f"seed_sigma_{name}", sigma) for name, sigma in zip(STATE_NAMES, SEED_SIGMAS, strict=True))
    rows.extend(
        (
            ("measurement_sigma_m", MEASUREMENT_SIGMA_M),
            ("start_ns", START_NS),
            ("horizon_ns", ENCOUNTER.horizon_ns),
            ("contact_radius_m", ENCOUNTER.contact_radius_m),
        )
    )
    return "name,value\n" + "".join(numbers([name, value]) + "\n" for name, value in rows)


def motion_rows() -> str:
    """motion.csv's text."""
    names = [f"m_{row}_{column}" for row in range(STATE_SIZE) for column in range(STATE_SIZE)]
    lines = [",".join(["model", "dt_ns", "matrix", *names]) + "\n"]
    for number, model in enumerate(MODELS):
        for dt_ns in DT_NS:
            dt_s = float(dt_ns) / NS_PER_S
            for name, matrix in (("transition", transition(model, dt_s)), ("noise", process_noise(model, dt_s))):
                lines.append(numbers([number, dt_ns, name, *(float(value) for value in matrix.flatten())]) + "\n")
    return "".join(lines)


def measurement_rows(measurements: list[Measurement]) -> str:
    """measurements.csv's text: the target's, then the interceptor's."""
    lines = ["body,time_ns,east,north,up\n"]
    half = len(measurements) // 2
    for number, measurement in enumerate(measurements):
        body = BODIES[0] if number < half else BODIES[1]
        lines.append(numbers([body, measurement.time_ns, *(float(value) for value in measurement.position)]) + "\n")
    return "".join(lines)


def written(number: int, count: int) -> bool:
    """Whether the state after measurement number of count is written."""
    return (number + 1) % WRITE_EVERY == 0 or number == count - 1


def track_rows(tracks: tuple[list[ImmState], list[ImmState]]) -> str:
    """tracks.csv's text."""
    chances = [f"p_{number}" for number in range(len(MODELS))]
    lines = [",".join(["body", "time_ns", *chances, *STATE_NAMES, *upper_names("cov", STATE_SIZE)]) + "\n"]
    for body, states in zip(BODIES, tracks, strict=True):
        for number, state in enumerate(states):
            if written(number, len(states)):
                estimate = combined(state)
                values = [*(float(p) for p in state.probabilities), *(float(v) for v in estimate.state)]
                lines.append(numbers([body, state.time_ns, *values, *upper(estimate.covariance)]) + "\n")
    return "".join(lines)


def encounter_rows(target: list[ImmState], interceptor: list[ImmState]) -> str:
    """encounters.csv's text."""
    lines = ["time_ns,found,closest_ns,miss_m,miss_sigma_m,contact_ns\n"]
    for number, (first, second) in enumerate(zip(target, interceptor, strict=True)):
        if written(number, len(target)):
            found = encounter(combined(first), combined(second), ENCOUNTER)
            lines.append(numbers([first.time_ns, *encounter_fields(found)]) + "\n")
    return "".join(lines)


def edge_cases() -> list[tuple[Vector, Vector]]:
    """Target and interceptor states for the edge cases, in order.

    Receding, parked, past the horizon, in contact already, a direct hit with
    no relative acceleration, an offset pass while accelerating, falling short
    and turning back, and receding then coming back into contact.
    """
    zero = np.zeros(STATE_SIZE)
    cases = (
        (5.0, 0.0, 0.0, 10.0, 0.0, 0.0, 0.0, 0.0, 0.0),
        (5.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0),
        (-500.0, 1.0, 0.0, 10.0, 0.0, 0.0, 0.0, 0.0, 0.0),
        (0.2, 0.1, 0.0, -30.0, 1.0, 0.0, 0.0, 0.0, 0.0),
        (-20.0, 0.0, 0.0, 20.0, 0.0, 0.0, 0.0, 0.0, 0.0),
        (-40.0, 3.0, 1.0, 20.0, 0.0, 0.0, 2.0, 0.0, 0.0),
        (-20.0, 1.0, 0.0, 20.0, 0.0, 0.0, -15.0, 0.0, 0.0),
        (5.0, 0.2, 0.0, 10.0, 0.0, 0.0, -20.0, 0.0, 0.0),
    )
    return [(zero, np.array(case)) for case in cases]


def random_cases(rng: np.random.Generator) -> list[tuple[Vector, Vector]]:
    """Random target and interceptor states closing from up to 100 m apart."""
    cases: list[tuple[Vector, Vector]] = []
    for _ in range(RANDOM_APPROACHES):
        target = np.concatenate(
            [rng.uniform(-50.0, 50.0, AXES), rng.uniform(-30.0, 30.0, AXES), rng.uniform(-5.0, 5.0, AXES)]
        )
        offset = rng.uniform(-50.0, 50.0, AXES)
        closing = -offset * rng.uniform(0.2, 2.0) + rng.uniform(-3.0, 3.0, AXES)
        interceptor = target + np.concatenate([offset, closing, rng.uniform(-10.0, 10.0, AXES)])
        cases.append((target, interceptor))
    return cases


def approach_rows(rng: np.random.Generator) -> str:
    """approaches.csv's text."""
    names = [f"{body}_{name}" for body in BODIES for name in STATE_NAMES]
    sigmas = [f"{body}_sigma_{name}" for body in BODIES for name in STATE_NAMES]
    result = ["found", "closest_ns", "miss_m", "miss_sigma_m", "contact_ns"]
    lines = [",".join(["case", "time_ns", *names, *sigmas, *result]) + "\n"]
    for case, (target, interceptor) in enumerate([*edge_cases(), *random_cases(rng)]):
        spreads = rng.uniform(0.001, 0.5, (2, STATE_SIZE))
        first = Estimate(START_NS, target, np.diag(np.square(spreads[0])))
        second = Estimate(START_NS, interceptor, np.diag(np.square(spreads[1])))
        found = encounter(first, second, ENCOUNTER)
        states = [float(value) for value in (*target, *interceptor)]
        lines.append(
            numbers([case, START_NS, *states, *(float(s) for s in spreads.flatten()), *encounter_fields(found)])
        )
        lines[-1] += "\n"
    return "".join(lines)


def write(directory: Path) -> None:
    """Write every golden file into directory."""
    directory.mkdir(parents=True, exist_ok=True)
    rng = np.random.default_rng(SEED)
    measurements, target, interceptor = engagement(rng, MEASUREMENT_SIGMA_M)
    (directory / "parameters.csv").write_text(parameter_rows())
    (directory / "motion.csv").write_text(motion_rows())
    (directory / "measurements.csv").write_text(measurement_rows(measurements))
    (directory / "tracks.csv").write_text(track_rows((target, interceptor)))
    (directory / "encounters.csv").write_text(encounter_rows(target, interceptor))
    (directory / "approaches.csv").write_text(approach_rows(rng))


def main(argv: list[str]) -> int:
    """Write the golden files into the directory named."""
    if len(argv) != 1:
        sys.stderr.write("usage: python -m ics_golden.imm DIRECTORY\n")
        return EXIT_USAGE
    write(Path(argv[0]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
