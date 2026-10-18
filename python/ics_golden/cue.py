"""Golden vectors for the cue extrapolator (ICS-037), which the C++ port, ics::cue, must match.

Each file is CSV with a header, every number printed to round-trip exactly:

- parameters.csv: the filter's settings;
- measurements.csv: PLI positions and velocities, as WGS 84 points with ENU
  velocities at the vehicle, and the same in the range ENU frame. The first
  point is the range origin the other files use;
- reports.csv: eight seconds of a manoeuvring target, in arrival order. Each
  row is a report: GNSS PLI at 10 Hz with velocity, arriving 100 ms late, or
  radar at 2 Hz without, arriving 300 ms late, one radar report 50 m off and
  one PLI report 2.5 s late. Then the outcome, and the estimate at the newest
  report held afterwards;
- queries.csv: estimates asked for after the last report, up to and past the coast limit;
- cues.csv: three stations' cues for estimates from the run, and one target overhead.

The port must give every outcome and refusal, every state and covariance
value within 1e-9 of the larger of it and 1, and every angle within 1e-10 rad.
Its frames module puts a station in the range frame by other arithmetic, about
1e-9 m from this reference's.

Regenerate from python/ after changing the reference:

    uv run --locked python -m ics_golden.cue ../golden/cue
"""

import sys
from collections.abc import Iterable
from dataclasses import dataclass
from pathlib import Path

import numpy as np

from ics_models.cue.filter import CueError, CueFilter, FilterSettings, Measurement
from ics_models.cue.geometry import Geodetic, axes_between, to_enu
from ics_models.cue.kinematics import AXES, STATE_SIZE, Estimate, ProcessNoise, Vector
from ics_models.cue.pointing import cue_for, station_at

EXIT_USAGE: int = 2
SEED: int = 37
ORIGIN = Geodetic(40.0, -100.0, 700.0)
SETTINGS = FilterSettings(
    noise=ProcessNoise(horizontal=4.0, vertical=1.0),
    gate=16.27,
    history_ns=2_000_000_000,
    coast_limit_ns=2_000_000_000,
    capacity=64,
    initial_velocity_sigma_m_s=20.0,
    initial_acceleration_sigma_m_s2=10.0,
)
DURATION_NS: int = 8_000_000_000
PLI_PERIOD_NS: int = 100_000_000
PLI_LATENCY_NS: int = 100_000_000
RADAR_PERIOD_NS: int = 500_000_000
RADAR_LATENCY_NS: int = 300_000_000
STRAGGLER_LATENCY_NS: int = 2_500_000_000
STRAGGLER_INDEX: int = 40
OUTLIER_INDEX: int = 9
OUTLIER_OFFSET_M: float = 50.0
PLI_SIGMAS: tuple[float, float, float] = (0.5, 0.8, 0.2)
RADAR_SIGMA_M: float = 3.0
# Stations about 250 to 300 m from the crossing point, 2 m above the origin's height, and one under the target.
STATIONS: tuple[Geodetic, ...] = (
    Geodetic(39.9977477, -100.0, 702.0),
    Geodetic(40.0010812, -99.9969537, 702.0),
    Geodetic(40.0018919, -100.0021096, 702.0),
)
QUERY_OFFSETS_NS: tuple[int, ...] = (0, 20_000_000, 300_000_000, 2_000_000_000, 2_000_000_001)
CUE_EVERY: int = 10
UPPER: tuple[tuple[int, int], ...] = tuple(
    (row, column) for row in range(STATE_SIZE) for column in range(row, STATE_SIZE)
)
POSITION_UPPER: tuple[tuple[int, int], ...] = tuple((row, column) for row in range(AXES) for column in range(row, AXES))


@dataclass(frozen=True)
class Arrival:
    """A report and when it reaches the filter."""

    arrival_ns: int
    measurement: Measurement


def truth(time_ns: int) -> tuple[Vector, Vector]:
    """The target's position and velocity: east at 5 m/s, then a turn north at about 0.3 g."""
    seconds = time_ns / 1e9
    turn_s = 3.0
    position = np.array([-60.0 + 5.0 * seconds, -20.0, 35.0 + 0.5 * seconds])
    velocity = np.array([5.0, 0.0, 0.5])
    if seconds > turn_s:
        after = seconds - turn_s
        position[1] += 1.5 * after * after
        velocity[1] += 3.0 * after
    return position, velocity


def as_tuple(values: Vector) -> tuple[float, float, float]:
    """Three numbers as a tuple."""
    return (float(values[0]), float(values[1]), float(values[2]))


def arrivals() -> list[Arrival]:
    """The run's reports, in arrival order."""
    rng = np.random.default_rng(SEED)
    sigma_h, sigma_v, sigma_velocity = PLI_SIGMAS
    reports: list[Arrival] = []
    for number, time_ns in enumerate(range(0, DURATION_NS, PLI_PERIOD_NS)):
        position, velocity = truth(time_ns)
        noisy = position + rng.normal(0.0, 1.0, AXES) * np.array([sigma_h, sigma_h, sigma_v])
        moving = velocity + rng.normal(0.0, sigma_velocity, AXES)
        measured = Measurement(time_ns, as_tuple(noisy), sigma_h, sigma_v, as_tuple(moving), sigma_velocity)
        latency = STRAGGLER_LATENCY_NS if number == STRAGGLER_INDEX else PLI_LATENCY_NS
        reports.append(Arrival(time_ns + latency, measured))
    for number, time_ns in enumerate(range(RADAR_PERIOD_NS // 2, DURATION_NS, RADAR_PERIOD_NS)):
        position, _ = truth(time_ns)
        noisy = position + rng.normal(0.0, RADAR_SIGMA_M, AXES)
        if number == OUTLIER_INDEX:
            noisy += OUTLIER_OFFSET_M
        measured = Measurement(time_ns, as_tuple(noisy), RADAR_SIGMA_M, RADAR_SIGMA_M, None, 0.0)
        reports.append(Arrival(time_ns + RADAR_LATENCY_NS, measured))
    return sorted(reports, key=lambda arrival: (arrival.arrival_ns, arrival.measurement.time_ns))


def numbers(values: Iterable[float | int]) -> str:
    """Values joined by commas, each printed to round-trip."""
    return ",".join(repr(float(value)) if isinstance(value, float) else str(value) for value in values)


def estimate_fields(estimate: Estimate) -> list[float]:
    """An estimate's state, then its covariance's upper triangle, row by row."""
    return [float(value) for value in estimate.state] + [
        float(estimate.covariance[row, column]) for row, column in UPPER
    ]


def state_header(prefix: str) -> list[str]:
    """The column names of estimate_fields."""
    return [f"{prefix}x{index}" for index in range(STATE_SIZE)] + [f"{prefix}p{row}{column}" for row, column in UPPER]


def report_fields(measurement: Measurement) -> list[float | int]:
    """A report's columns: time, position, sigmas, then velocity and its sigma, or zeros without one."""
    velocity = measurement.velocity if measurement.velocity is not None else (0.0, 0.0, 0.0)
    has_velocity = 1 if measurement.velocity is not None else 0
    return [
        measurement.time_ns,
        *measurement.position,
        measurement.horizontal_sigma_m,
        measurement.vertical_sigma_m,
        has_velocity,
        *velocity,
        measurement.velocity_sigma_m_s,
    ]


REPORT_HEADER: tuple[str, ...] = (
    "time_ns",
    "east_m",
    "north_m",
    "up_m",
    "horizontal_sigma_m",
    "vertical_sigma_m",
    "has_velocity",
    "velocity_east_m_s",
    "velocity_north_m_s",
    "velocity_up_m_s",
    "velocity_sigma_m_s",
)


def run() -> tuple[str, CueFilter, list[Estimate]]:
    """reports.csv's text, the filter after the run, and every estimate it gave."""
    cue_filter = CueFilter(SETTINGS)
    lines = [",".join([*REPORT_HEADER, "outcome", "latest_ns", *state_header("")]) + "\n"]
    estimates: list[Estimate] = []
    for arrival in arrivals():
        outcome = cue_filter.add(arrival.measurement)
        latest = cue_filter.latest_ns()
        if latest is None:
            message = "the filter holds no report after one was added"
            raise CueError(message)
        estimate = cue_filter.estimate_at(latest)
        estimates.append(estimate)
        fields = [*report_fields(arrival.measurement), outcome.value, latest, *estimate_fields(estimate)]
        lines.append(",".join(numbers([value]) if not isinstance(value, str) else value for value in fields) + "\n")
    return "".join(lines), cue_filter, estimates


def queries(cue_filter: CueFilter) -> str:
    """queries.csv's text: estimates after the last report, and a refusal past the coast limit."""
    latest = cue_filter.latest_ns()
    if latest is None:
        message = "the run left no report"
        raise CueError(message)
    lines = [",".join(["time_ns", "ok", *state_header("")]) + "\n"]
    for offset in QUERY_OFFSETS_NS:
        time_ns = latest + offset
        try:
            fields = [1, *estimate_fields(cue_filter.estimate_at(time_ns))]
        except CueError:
            fields = [0] + [0.0] * (STATE_SIZE + len(UPPER))
        lines.append(numbers([time_ns, *fields]) + "\n")
    return "".join(lines)


def cue_row(site: Geodetic, estimate: Estimate) -> str:
    """A row of cues.csv: the station, the estimate's time, position, velocity, position covariance, then the cue."""
    inputs = [
        site.latitude_deg,
        site.longitude_deg,
        site.height_m,
        estimate.time_ns,
        *[float(value) for value in estimate.state[0 : 2 * AXES]],
        *[float(estimate.covariance[row, column]) for row, column in POSITION_UPPER],
    ]
    try:
        cue = cue_for(estimate, station_at(ORIGIN, site))
        outputs: list[float | int] = [
            1,
            cue.azimuth_rad,
            cue.elevation_rad,
            cue.azimuth_rate_rad_s,
            cue.elevation_rate_rad_s,
            cue.range_m,
            cue.sigma_rad,
        ]
    except CueError:
        outputs = [0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0]
    return numbers([*inputs, *outputs]) + "\n"


CUE_HEADER: tuple[str, ...] = (
    "latitude_deg",
    "longitude_deg",
    "height_m",
    "time_ns",
    *[f"x{index}" for index in range(2 * AXES)],
    *[f"p{row}{column}" for row, column in POSITION_UPPER],
    "ok",
    "azimuth_rad",
    "elevation_rad",
    "azimuth_rate_rad_s",
    "elevation_rate_rad_s",
    "range_m",
    "sigma_rad",
)


def cues(estimates: list[Estimate]) -> str:
    """cues.csv's text."""
    lines = [",".join(CUE_HEADER) + "\n"]
    for estimate in estimates[::CUE_EVERY]:
        lines.extend(cue_row(site, estimate) for site in STATIONS)
    overhead = Estimate(0, np.zeros(STATE_SIZE), np.eye(STATE_SIZE))
    overhead.state[0:AXES] = to_enu(ORIGIN, STATIONS[0]) + np.array([0.0, 0.0, 40.0])
    lines.append(cue_row(STATIONS[0], overhead))
    return "".join(lines)


def measurements() -> str:
    """measurements.csv's text: points and their velocities, in the range ENU frame. The first is the origin."""
    header = "latitude_deg,longitude_deg,height_m,velocity_east_m_s,velocity_north_m_s,velocity_up_m_s"
    lines = [header + ",east_m,north_m,up_m,range_velocity_east_m_s,range_velocity_north_m_s,range_velocity_up_m_s\n"]
    velocity = np.array([5.0, -3.0, 1.0])
    for site in (ORIGIN, *STATIONS, Geodetic(40.01, -100.02, 1250.0)):
        position = to_enu(ORIGIN, site)
        turned = axes_between(site, ORIGIN) @ velocity
        values = [site.latitude_deg, site.longitude_deg, site.height_m, *velocity, *position, *turned]
        lines.append(numbers([float(value) for value in values]) + "\n")
    return "".join(lines)


def parameters() -> str:
    """parameters.csv's text: the filter's settings. The range origin is measurements.csv's first point."""
    values: list[tuple[str, float | int]] = [
        ("horizontal_jerk_psd", SETTINGS.noise.horizontal),
        ("vertical_jerk_psd", SETTINGS.noise.vertical),
        ("gate", SETTINGS.gate),
        ("history_ns", SETTINGS.history_ns),
        ("coast_limit_ns", SETTINGS.coast_limit_ns),
        ("capacity", SETTINGS.capacity),
        ("initial_velocity_sigma_m_s", SETTINGS.initial_velocity_sigma_m_s),
        ("initial_acceleration_sigma_m_s2", SETTINGS.initial_acceleration_sigma_m_s2),
    ]
    return "name,value\n" + "".join(f"{name},{numbers([value])}\n" for name, value in values)


def write(directory: Path) -> None:
    """Write every golden file into directory."""
    directory.mkdir(parents=True, exist_ok=True)
    reports, cue_filter, estimates = run()
    (directory / "parameters.csv").write_text(parameters())
    (directory / "measurements.csv").write_text(measurements())
    (directory / "reports.csv").write_text(reports)
    (directory / "queries.csv").write_text(queries(cue_filter))
    (directory / "cues.csv").write_text(cues(estimates))


def main(argv: list[str]) -> int:
    """Write the golden files into the directory named."""
    if len(argv) != 1:
        sys.stderr.write("usage: python -m ics_golden.cue DIRECTORY\n")
        return EXIT_USAGE
    write(Path(argv[0]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
