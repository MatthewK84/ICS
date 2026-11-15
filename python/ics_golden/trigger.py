"""Golden vectors for the trigger controller (ICS-040), which the C++ port, ics::trigger, must match.

Each CSV file has a header, and every number is printed to round-trip:

- parameters.csv: the settings the other files were written with;
- approaches.csv: refined_approach for a target and an interceptor at random
  states with accelerations, gentle and violent, with found = 0 when there is
  no approach to predict;
- shots.csv: shot_for and window_open for pulses asked for at random times
  around now, in time for their own edge and too late for it.

python -m ics_golden.trigger DIRECTORY writes them.
"""

import math
import sys
from collections.abc import Iterable
from pathlib import Path

import numpy as np

from ics_models.cue.kinematics import STATE_SIZE, Estimate
from ics_models.handoff.approach import ApproachSettings
from ics_models.trigger.approach import refined_approach
from ics_models.trigger.shot import ShotSettings, shot_for, window_open

EXIT_USAGE: int = 2
SEED: int = 40
START_NS: int = 1_760_000_000_000_000_000
NS_PER_S: int = 1_000_000_000
APPROACHES: int = 60
SHOTS: int = 60
KINDS: int = 6
APPROACH = ApproachSettings(launch_speed_m_s=3.0, horizon_ns=60 * NS_PER_S)
SHOT = ShotSettings(period_ns=NS_PER_S, offset_ns=1_500, command_margin_ns=200_000_000, guard_ns=50_000_000)
AXIS_NAMES: tuple[str, ...] = ("east", "north", "up", "v_east", "v_north", "v_up", "a_east", "a_north", "a_up")


def numbers(values: Iterable[float | int | str]) -> str:
    """Values joined by commas, each float printed to round-trip."""
    return ",".join(repr(float(value)) if isinstance(value, float) else str(value) for value in values)


def parameter_rows() -> str:
    """parameters.csv's text."""
    values: tuple[tuple[str, float | int], ...] = (
        ("launch_speed_m_s", APPROACH.launch_speed_m_s),
        ("horizon_ns", APPROACH.horizon_ns),
        ("period_ns", SHOT.period_ns),
        ("offset_ns", SHOT.offset_ns),
        ("command_margin_ns", SHOT.command_margin_ns),
        ("guard_ns", SHOT.guard_ns),
    )
    return "name,value\n" + "".join(numbers([name, value]) + "\n" for name, value in values)


def approach_case(rng: np.random.Generator, kind: int) -> tuple[Estimate, Estimate]:
    """A target and an interceptor: kind 0 parked, 1 receding, 2 violent, any other closing gently."""
    target = [float(value) for value in rng.uniform([-300.0, -300.0, 20.0], [300.0, 300.0, 80.0])]
    target_velocity = [float(value) for value in rng.normal(0.0, 6.0, 3)]
    offset = [float(value) for value in rng.normal(0.0, 150.0, 3)]
    aim = [-value + float(rng.normal(0.0, 4.0)) for value in offset]
    distance = math.sqrt(sum(value * value for value in aim))
    speeds = (float(rng.uniform(0.0, 2.9)), -float(rng.uniform(10.0, 40.0)))
    speed = speeds[kind] if kind < len(speeds) else float(rng.uniform(10.0, 40.0))
    spread = 60.0 if kind == len(speeds) else 2.0
    position = [target[axis] + offset[axis] for axis in range(3)]
    velocity = [speed * value / distance for value in aim]
    accelerations = [float(value) for value in rng.normal(0.0, spread, 6)]
    first = Estimate(START_NS, np.array([*target, *target_velocity, *accelerations[0:3]]), np.eye(STATE_SIZE))
    second = Estimate(START_NS, np.array([*position, *velocity, *accelerations[3:6]]), np.eye(STATE_SIZE))
    return first, second


def approach_rows(rng: np.random.Generator) -> str:
    """approaches.csv's text."""
    header = ",".join(f"{vehicle}_{name}" for vehicle in ("target", "interceptor") for name in AXIS_NAMES)
    lines = ["time_ns," + header + ",found,approach_time_ns,miss_m\n"]
    for case in range(APPROACHES):
        target, interceptor = approach_case(rng, case % KINDS)
        found = refined_approach(target, interceptor, APPROACH)
        states = [*(float(value) for value in target.state), *(float(value) for value in interceptor.state)]
        outcome = [0, 0, 0.0] if found is None else [1, found.time_ns, found.miss_m]
        lines.append(numbers([START_NS, *states, *outcome]) + "\n")
    return "".join(lines)


def shot_rows(rng: np.random.Generator) -> str:
    """shots.csv's text: the pulse is asked for up to 3 s after now, or up to 1 s before it."""
    lines = ["fire_ns,now_ns,edge_ns,delay_ns,late,open\n"]
    for case in range(SHOTS):
        now_ns = START_NS + int(rng.integers(0, 5 * NS_PER_S))
        ahead_ns = int(rng.integers(-NS_PER_S, 3 * NS_PER_S))
        fire_ns = now_ns + ahead_ns if case % 5 else now_ns - now_ns % NS_PER_S + SHOT.offset_ns + 2 * NS_PER_S
        shot = shot_for(fire_ns, now_ns, SHOT)
        opened = window_open(shot, now_ns, SHOT)
        lines.append(numbers([fire_ns, now_ns, shot.edge_ns, shot.delay_ns, int(shot.late), int(opened)]) + "\n")
    return "".join(lines)


def write(directory: Path) -> None:
    """Write every golden file into directory."""
    directory.mkdir(parents=True, exist_ok=True)
    rng = np.random.default_rng(SEED)
    (directory / "parameters.csv").write_text(parameter_rows())
    (directory / "approaches.csv").write_text(approach_rows(rng))
    (directory / "shots.csv").write_text(shot_rows(rng))


def main(argv: list[str]) -> int:
    """Write the golden files into the directory named."""
    if len(argv) != 1:
        sys.stderr.write("usage: python -m ics_golden.trigger DIRECTORY\n")
        return EXIT_USAGE
    write(Path(argv[0]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
