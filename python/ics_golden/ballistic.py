"""Golden vectors for the 7-state ballistic model (ICS-044), which the C++ port (CPP-28, ICS-073) must match.

Each CSV file has a header, and every number is printed to round-trip:

- parameters.csv: the settings the other files were written with;
- atmosphere.csv: ISA density from -500 m to 11 km;
- trajectories.csv: six pieces, from no drag to foil-like drag, propagated to
  times that fall between RK4 steps as well as on them;
- measurements.csv: those pieces' positions every 10 ms for 2 s, with
  Gaussian noise of SIGMA_M on each axis;
- fits.csv: for each piece, its true state, the fitted state, the fit's
  iterations, convergence and RMS residual, and the covariance's upper
  triangle, row by row.

python -m ics_golden.ballistic DIRECTORY writes them.
"""

import sys
from collections.abc import Iterable
from pathlib import Path

import numpy as np

from ics_models.ballistic.atmosphere import LOWEST_HEIGHT_M, TROPOPAUSE_HEIGHT_M, density
from ics_models.ballistic.dynamics import STATE_SIZE, Model, Vector, trajectory
from ics_models.ballistic.fit import FitResult, FitSettings, fit

EXIT_USAGE: int = 2
SEED: int = 44
START_NS: int = 1_760_000_000_000_000_000
MODEL = Model(origin_height_m=1_200.0)
SIGMA_M: float = 0.02
SETTINGS = FitSettings(model=MODEL, sigma_m=SIGMA_M)
KAPPAS: tuple[float, ...] = (0.0, 0.005, 0.02, 0.08, 0.3, 1.0)
HEIGHTS: int = 24
SAMPLES: int = 41
SAMPLE_NS: int = 73_000_000
POINTS: int = 201
POINT_NS: int = 10_000_000
STATE_NAMES: tuple[str, ...] = ("east", "north", "up", "v_east", "v_north", "v_up", "kappa")


def numbers(values: Iterable[float | int | str]) -> str:
    """Values joined by commas, each float printed to round-trip."""
    return ",".join(repr(float(value)) if isinstance(value, float) else str(value) for value in values)


def parameter_rows() -> str:
    """parameters.csv's text."""
    values: tuple[tuple[str, float | int], ...] = (
        ("origin_height_m", MODEL.origin_height_m),
        ("gravity_m_s2", MODEL.gravity_m_s2),
        ("step_ns", MODEL.step_ns),
        ("sigma_m", SETTINGS.sigma_m),
        ("initial_kappa_m2_kg", SETTINGS.initial_kappa_m2_kg),
        ("max_iterations", SETTINGS.max_iterations),
        ("step_tolerance", SETTINGS.step_tolerance),
        ("initial_damping", SETTINGS.initial_damping),
    )
    return "name,value\n" + "".join(numbers([name, value]) + "\n" for name, value in values)


def atmosphere_rows() -> str:
    """atmosphere.csv's text."""
    heights = np.linspace(LOWEST_HEIGHT_M, TROPOPAUSE_HEIGHT_M, HEIGHTS)
    return "height_m,density_kg_m3\n" + "".join(numbers([float(h), density(float(h))]) + "\n" for h in heights)


def truths(rng: np.random.Generator) -> list[Vector]:
    """One piece per κ: 40 to 200 m up, thrown at up to 15 m/s."""
    pieces: list[Vector] = []
    for kappa in KAPPAS:
        position = rng.uniform([-100.0, -100.0, 40.0], [100.0, 100.0, 200.0])
        velocity = rng.uniform(-15.0, 15.0, 3)
        pieces.append(np.array([*position, *velocity, kappa]))
    return pieces


def trajectory_rows(pieces: list[Vector]) -> str:
    """trajectories.csv's text: each piece from START_NS, every SAMPLE_NS."""
    lines = ["case,time_ns," + ",".join(STATE_NAMES) + "\n"]
    times = [START_NS + sample * SAMPLE_NS for sample in range(SAMPLES)]
    for case, piece in enumerate(pieces):
        path = trajectory(piece, START_NS, times, MODEL)
        lines.extend(
            numbers([case, time, *(float(v) for v in row)]) + "\n" for time, row in zip(times, path, strict=True)
        )
    return "".join(lines)


def measured(piece: Vector, rng: np.random.Generator) -> tuple[list[int], np.ndarray]:
    """A piece's noisy positions every POINT_NS from START_NS."""
    times = [START_NS + point * POINT_NS for point in range(POINTS)]
    path = trajectory(piece, START_NS, times, MODEL)[:, 0:3]
    return times, path + rng.normal(0.0, SIGMA_M, path.shape)


def fit_row(case: int, piece: Vector, result: FitResult) -> str:
    """One fits.csv row."""
    upper = [float(result.covariance[row, column]) for row in range(STATE_SIZE) for column in range(row, STATE_SIZE)]
    truth, state = (float(v) for v in piece), (float(v) for v in result.state)
    outcome = [result.iterations, int(result.converged), result.residual_rms_m]
    return numbers([case, result.epoch_ns, *truth, *state, *outcome, *upper]) + "\n"


def fit_files(pieces: list[Vector], rng: np.random.Generator) -> tuple[str, str]:
    """measurements.csv's and fits.csv's text."""
    points = ["case,time_ns,east,north,up\n"]
    names = [f"cov_{row}_{column}" for row in range(STATE_SIZE) for column in range(row, STATE_SIZE)]
    header = ["case", "epoch_ns", *(f"true_{n}" for n in STATE_NAMES), *(f"fit_{n}" for n in STATE_NAMES)]
    fits = [",".join([*header, "iterations", "converged", "residual_rms_m", *names]) + "\n"]
    for case, piece in enumerate(pieces):
        times, positions = measured(piece, rng)
        points.extend(numbers([case, t, *(float(v) for v in p)]) + "\n" for t, p in zip(times, positions, strict=True))
        fits.append(fit_row(case, piece, fit(times, positions, SETTINGS)))
    return "".join(points), "".join(fits)


def write(directory: Path) -> None:
    """Write every golden file into directory."""
    directory.mkdir(parents=True, exist_ok=True)
    rng = np.random.default_rng(SEED)
    pieces = truths(rng)
    measurements, fits = fit_files(pieces, rng)
    (directory / "parameters.csv").write_text(parameter_rows())
    (directory / "atmosphere.csv").write_text(atmosphere_rows())
    (directory / "trajectories.csv").write_text(trajectory_rows(pieces))
    (directory / "measurements.csv").write_text(measurements)
    (directory / "fits.csv").write_text(fits)


def main(argv: list[str]) -> int:
    """Write the golden files into the directory named."""
    if len(argv) != 1:
        sys.stderr.write("usage: python -m ics_golden.ballistic DIRECTORY\n")
        return EXIT_USAGE
    write(Path(argv[0]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
