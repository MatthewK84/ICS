"""Golden vectors for the hand-off scheduler (ICS-039), which the C++ port, ics::handoff, must match.

Each CSV file has a header, and every number is printed to round-trip:

- parameters.csv: the settings the other files were written with;
- approaches.csv: closest_approach for a target and an interceptor at random
  states, parked, closing, receding and past the horizon, with found = 0 when
  there is no approach to predict;
- slews.csv: slew_demand for targets and mounts at random states, with errors
  small enough for the gain, large enough for the deceleration, and large
  enough to ask for more than the rate limit;
- reaches.csv: reach_seconds between random directions;
- queue.csv: a scripted run of four engagements, a row for each engagement at
  each update: the sighting given, and the slot, the order and the engagement
  served after it. The run has an order that changes with launches, changes
  inside and past the margin, a fly-by, an approach given up, an approach
  lost as it passes, engagements too close together and one out of reach.

python -m ics_golden.handoff DIRECTORY writes them.
"""

import math
import sys
from collections.abc import Iterable
from dataclasses import dataclass
from pathlib import Path

import numpy as np

from ics_models.cue.kinematics import STATE_SIZE, Estimate
from ics_models.handoff.approach import Approach, ApproachSettings, closest_approach
from ics_models.handoff.queue import Plan, Queue, QueueSettings, Sighting
from ics_models.handoff.slew import SlewLimits, reach_seconds, slew_demand
from ics_models.pointing.model import AltAz
from ics_models.tracker.demand import AxisState

EXIT_USAGE: int = 2
SEED: int = 39
START_NS: int = 1_760_000_000_000_000_000
NS_PER_S: int = 1_000_000_000
APPROACHES: int = 48
SLEWS: int = 48
REACHES: int = 24
STEP_NS: int = 100_000_000
STEPS: int = 220
APPROACH = ApproachSettings(launch_speed_m_s=3.0, horizon_ns=60 * NS_PER_S)
LIMITS = SlewLimits(rate_rad_s=math.radians(10.0), acceleration_rad_s2=0.5, gain=6.0)
QUEUE = QueueSettings(
    min_gap_ns=3 * NS_PER_S,
    dwell_ns=NS_PER_S // 2,
    margin_ns=NS_PER_S // 2,
    settle_ns=300_000_000,
    engaged_miss_m=10.0,
)


@dataclass(frozen=True)
class Pass:
    """An approach in the script: seen from a time until it is given up or passes, in seconds from the start."""

    seen_from_s: float
    seen_until_s: float
    passes_s: float
    miss_m: float


@dataclass(frozen=True)
class Scripted:
    """An engagement in the script: its approaches, and its target's direction, drifting, once the station has it."""

    passes: tuple[Pass, ...]
    sighted_from_s: float
    azimuth: float
    elevation: float
    azimuth_rate: float


# Engagement 0 launches second and is hit first. Engagement 1 follows 2.5 s later, 60° away: too close and out of
# reach. Engagement 2 flies by at 30 m, then comes back. Engagement 3's first predictions are wrong by less than
# the margin, then by more; it gives its approach up, takes it again, and loses it 0.25 s before it passes.
SCRIPT: tuple[Scripted, ...] = (
    Scripted((Pass(2.0, 8.0, 8.0, 1.2),), 0.0, 1.0, 0.30, 0.004),
    Scripted((Pass(1.0, 10.5, 10.5, 0.6),), 0.0, 2.05, 0.32, 0.002),
    Scripted((Pass(9.0, 15.0, 15.0, 30.0), Pass(17.5, 19.0, 19.0, 2.0)), 0.0, 2.40, 0.35, -0.003),
    Scripted(
        (
            Pass(12.0, 12.55, 14.8, 3.0),
            Pass(12.55, 12.95, 14.2, 3.0),
            Pass(12.95, 13.95, 16.02, 3.0),
            Pass(14.55, 15.75, 16.02, 3.0),
        ),
        13.0,
        2.45,
        0.33,
        0.001,
    ),
)


def numbers(values: Iterable[float | int | str]) -> str:
    """Values joined by commas, each float printed to round-trip."""
    return ",".join(repr(float(value)) if isinstance(value, float) else str(value) for value in values)


def parameter_rows() -> str:
    """parameters.csv's text."""
    values: tuple[tuple[str, float | int], ...] = (
        ("launch_speed_m_s", APPROACH.launch_speed_m_s),
        ("horizon_ns", APPROACH.horizon_ns),
        ("rate_rad_s", LIMITS.rate_rad_s),
        ("acceleration_rad_s2", LIMITS.acceleration_rad_s2),
        ("gain", LIMITS.gain),
        ("min_gap_ns", QUEUE.min_gap_ns),
        ("dwell_ns", QUEUE.dwell_ns),
        ("margin_ns", QUEUE.margin_ns),
        ("settle_ns", QUEUE.settle_ns),
        ("engaged_miss_m", QUEUE.engaged_miss_m),
        ("engagements", len(SCRIPT)),
    )
    return "name,value\n" + "".join(numbers([name, value]) + "\n" for name, value in values)


def estimate_of(position: Iterable[float], velocity: Iterable[float]) -> Estimate:
    """An estimate at the start, with no acceleration; an approach does not read the covariance."""
    state = np.array([*position, *velocity, 0.0, 0.0, 0.0])
    return Estimate(START_NS, state, np.eye(STATE_SIZE))


def approach_case(rng: np.random.Generator, kind: int) -> tuple[Estimate, Estimate]:
    """A target and an interceptor: kind 0 parked, 1 receding, 2 closing slowly from afar, any other closing."""
    target = [float(value) for value in rng.uniform([-300.0, -300.0, 20.0], [300.0, 300.0, 80.0])]
    target_velocity = [float(value) for value in rng.normal(0.0, 6.0, 3)]
    offset = [float(value) for value in rng.normal(0.0, 150.0, 3)]
    aim = [-value + float(rng.normal(0.0, 4.0)) for value in offset]
    distance = math.sqrt(sum(value * value for value in aim))
    speeds = (float(rng.uniform(0.0, 2.9)), -float(rng.uniform(10.0, 40.0)), float(rng.uniform(3.1, 4.0)))
    speed = speeds[kind] if kind < len(speeds) else float(rng.uniform(10.0, 40.0))
    velocity = [speed * value / distance for value in aim]
    if kind == len(speeds) - 1:
        offset = [value * 40.0 for value in offset]
        target_velocity = [0.0, 0.0, 0.0]
    position = [target[axis] + offset[axis] for axis in range(3)]
    return estimate_of(target, target_velocity), estimate_of(position, velocity)


def approach_rows(rng: np.random.Generator) -> str:
    """approaches.csv's text."""
    header = "time_ns,target_east,target_north,target_up,target_v_east,target_v_north,target_v_up"
    header += ",interceptor_east,interceptor_north,interceptor_up,interceptor_v_east,interceptor_v_north"
    lines = [header + ",interceptor_v_up,found,approach_time_ns,miss_m\n"]
    for case in range(APPROACHES):
        target, interceptor = approach_case(rng, case % 6)
        found = closest_approach(target, interceptor, APPROACH)
        states = [*(float(value) for value in target.state[0:6]), *(float(value) for value in interceptor.state[0:6])]
        outcome = [0, 0, 0.0] if found is None else [1, found.time_ns, found.miss_m]
        lines.append(numbers([START_NS, *states, *outcome]) + "\n")
    return "".join(lines)


def slew_rows(rng: np.random.Generator) -> str:
    """slews.csv's text: the errors are small, middling and large by turns, and every third target is fast."""
    header = "target_time_ns,target_azimuth,target_elevation,target_azimuth_rate,target_elevation_rate"
    header += ",mount_time_ns,mount_azimuth,mount_elevation,mount_azimuth_rate,mount_elevation_rate"
    lines = [header + ",azimuth_rate,elevation_rate\n"]
    for case in range(SLEWS):
        lead_ns = int(rng.integers(0, 200_000_000))
        spread = (0.005, 0.025, 3.0)[case % 3]
        rate_spread = (0.02, 0.02, 0.3)[(case // 3) % 3]
        azimuth, elevation = float(rng.uniform(0.0, 6.28)), float(rng.uniform(0.1, 1.4))
        azimuth_rate, elevation_rate = (float(value) for value in rng.normal(0.0, rate_spread, 2))
        target = AxisState(START_NS + lead_ns, azimuth, elevation, azimuth_rate, elevation_rate)
        offset_azimuth, offset_elevation = (float(value) for value in rng.normal(0.0, spread, 2))
        mount_azimuth = (azimuth + offset_azimuth) % (2.0 * math.pi)
        mount = AxisState(START_NS, mount_azimuth, elevation + offset_elevation / 2.0, 0.05, -0.02)
        rates = slew_demand(target, mount, LIMITS)
        values = [target.time_ns, target.azimuth, target.elevation, target.azimuth_rate, target.elevation_rate]
        values += [mount.time_ns, mount.azimuth, mount.elevation, mount.azimuth_rate, mount.elevation_rate]
        lines.append(numbers([*values, *rates]) + "\n")
    return "".join(lines)


def reach_rows(rng: np.random.Generator) -> str:
    """reaches.csv's text: every other pair is close enough that the axes never reach their rate."""
    lines = ["first_azimuth,first_elevation,second_azimuth,second_elevation,seconds\n"]
    for case in range(REACHES):
        spread = 0.03 if case % 2 == 0 else 1.5
        first = AltAz(float(rng.uniform(0.0, 6.28)), float(rng.uniform(0.1, 1.2)))
        turn, rise = (float(value) for value in rng.normal(0.0, spread, 2))
        second = AltAz((first.azimuth + turn) % (2.0 * math.pi), first.elevation + rise / 8.0)
        seconds = reach_seconds(first, second, LIMITS)
        lines.append(numbers([first.azimuth, first.elevation, second.azimuth, second.elevation, seconds]) + "\n")
    return "".join(lines)


def sighting_of(scripted: Scripted, seconds: float, rng: np.random.Generator) -> Sighting:
    """What the station knows of a scripted engagement at a time: predictions wander less as the approach nears."""
    approach = None
    for scripted_pass in scripted.passes:
        if scripted_pass.seen_from_s <= seconds < scripted_pass.seen_until_s:
            wander = 0.002 * (scripted_pass.passes_s - seconds) * float(rng.standard_normal())
            time_ns = START_NS + math.floor((scripted_pass.passes_s + wander) * NS_PER_S + 0.5)
            approach = Approach(time_ns, scripted_pass.miss_m * (1.0 + 0.02 * float(rng.standard_normal())))
    direction = None
    if seconds >= scripted.sighted_from_s:
        direction = AltAz(scripted.azimuth + scripted.azimuth_rate * seconds, scripted.elevation)
    return Sighting(approach, direction)


def queue_line(step: int, engagement: int, sighting: Sighting, plan: Plan) -> str:
    """A row of queue.csv: an engagement's sighting at a step, and its slot and the plan after the update."""
    seen = [0, 0, 0.0] if sighting.approach is None else [1, sighting.approach.time_ns, sighting.approach.miss_m]
    direction = sighting.direction
    sighted = [0, 0.0, 0.0] if direction is None else [1, direction.azimuth, direction.elevation]
    slot = plan.slots[engagement]
    held = [0, 0, 0.0] if slot.approach is None else [1, slot.approach.time_ns, slot.approach.miss_m]
    place = plan.order.index(engagement) if engagement in plan.order else -1
    flags = [int(slot.done), *held, int(slot.too_close), int(slot.out_of_reach)]
    current = -1 if plan.current is None else plan.current
    now_ns = START_NS + step * STEP_NS
    return numbers([step, now_ns, engagement, *seen, *sighted, *flags, place, current, int(plan.dwelling)]) + "\n"


def queue_rows(rng: np.random.Generator) -> str:
    """queue.csv's text."""
    header = "step,now_ns,engagement,seen,seen_time_ns,seen_miss_m,sighted,azimuth,elevation"
    lines = [header + ",done,held,held_time_ns,held_miss_m,too_close,out_of_reach,place,current,dwelling\n"]
    queue = Queue(len(SCRIPT), QUEUE, LIMITS)
    for step in range(STEPS + 1):
        sightings = [sighting_of(scripted, step * STEP_NS / NS_PER_S, rng) for scripted in SCRIPT]
        plan = queue.update(START_NS + step * STEP_NS, sightings)
        lines.extend(queue_line(step, engagement, sighting, plan) for engagement, sighting in enumerate(sightings))
    return "".join(lines)


def write(directory: Path) -> None:
    """Write every golden file into directory."""
    directory.mkdir(parents=True, exist_ok=True)
    rng = np.random.default_rng(SEED)
    (directory / "parameters.csv").write_text(parameter_rows())
    (directory / "approaches.csv").write_text(approach_rows(rng))
    (directory / "slews.csv").write_text(slew_rows(rng))
    (directory / "reaches.csv").write_text(reach_rows(rng))
    (directory / "queue.csv").write_text(queue_rows(rng))


def main(argv: list[str]) -> int:
    """Write the golden files into the directory named."""
    if len(argv) != 1:
        sys.stderr.write("usage: python -m ics_golden.handoff DIRECTORY\n")
        return EXIT_USAGE
    write(Path(argv[0]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
