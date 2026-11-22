"""Golden vectors for the lift and flutter models and model selection (ICS-045), which the C++ port (CPP-28,
ICS-074) must match.

Each CSV file has a header, and every number is printed to round-trip. A
state is written in named columns: position, velocity, kappa, then
kappa_slope (flutter), lift_kappa and side_kappa (lift), with 0.0 for a
parameter the model does not have, which is the drag model's value.

- parameters.csv: the settings the other files were written with;
- truths.csv: six pieces, two for each model, and the model each follows;
- trajectories.csv: those pieces propagated to times between RK4 steps and on them;
- measurements.csv: their positions every 10 ms for 2 s, with Gaussian noise
  of SIGMA_M on each axis;
- fits.csv: every candidate model's fit to every piece, its BIC and its
  state's standard deviations;
- covariances.csv: each fit's covariance, row and column in the model's own state order;
- selection.csv: the model chosen for each piece.

python -m ics_golden.aero DIRECTORY writes them.
"""

import sys
from collections.abc import Iterable
from pathlib import Path

import numpy as np

from ics_golden.ballistic import MODEL, POINT_NS, POINTS, SAMPLE_NS, SAMPLES, SETTINGS, START_NS, numbers
from ics_models.ballistic.aero import FLUTTER, LIFT
from ics_models.ballistic.dynamics import STATE_SIZE, Matrix, Vector, propagate_with
from ics_models.ballistic.fit import BALLISTIC, FitModel
from ics_models.ballistic.select import Scored, Selection, select

EXIT_USAGE: int = 2
SEED: int = 45
# Each piece's model and its parameters from κ on.
CASES: tuple[tuple[FitModel, tuple[float, ...]], ...] = (
    (BALLISTIC, (0.02,)),
    (BALLISTIC, (0.2,)),
    (FLUTTER, (0.02, 0.005)),
    (FLUTTER, (0.1, 0.02)),
    (LIFT, (0.1, 0.05, 0.02)),
    (LIFT, (0.3, 0.15, -0.05)),
)
KINEMATIC_NAMES: tuple[str, ...] = ("east", "north", "up", "v_east", "v_north", "v_up")
PARAMETER_NAMES: tuple[str, ...] = ("kappa", "kappa_slope", "lift_kappa", "side_kappa")
# Where each model's parameters, from κ on, go among PARAMETER_NAMES.
PARAMETER_COLUMNS: tuple[tuple[str, tuple[int, ...]], ...] = (
    ("ballistic", (0,)),
    ("flutter", (0, 1)),
    ("lift", (0, 2, 3)),
)
STATE_NAMES: tuple[str, ...] = (*KINEMATIC_NAMES, *PARAMETER_NAMES)


def named(state: Vector, model: FitModel) -> list[float]:
    """A model's state in STATE_NAMES order, 0.0 for parameters it does not have."""
    parameters = [0.0] * len(PARAMETER_NAMES)
    columns = next(columns for name, columns in PARAMETER_COLUMNS if name == model.name)
    for offset, column in enumerate(columns):
        parameters[column] = float(state[STATE_SIZE - 1 + offset])
    return [*(float(value) for value in state[0 : STATE_SIZE - 1]), *parameters]


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


def truths(rng: np.random.Generator) -> list[Vector]:
    """One piece per case: 60 to 200 m up, thrown at up to 15 m/s."""
    pieces: list[Vector] = []
    for _, parameters in CASES:
        position = rng.uniform([-100.0, -100.0, 60.0], [100.0, 100.0, 200.0])
        velocity = rng.uniform(-15.0, 15.0, 3)
        pieces.append(np.array([*position, *velocity, *parameters]))
    return pieces


def path_of(piece: Vector, model: FitModel, times: list[int]) -> Matrix:
    """A piece's states at each time under its model."""
    return propagate_with(model.law, piece.reshape(1, model.law.size), START_NS, times, MODEL)[:, 0, :]


def truth_rows(pieces: list[Vector]) -> str:
    """truths.csv's text."""
    lines = ["case,model," + ",".join(STATE_NAMES) + "\n"]
    for case, ((model, _), piece) in enumerate(zip(CASES, pieces, strict=True)):
        lines.append(numbers([case, model.name, *named(piece, model)]) + "\n")
    return "".join(lines)


def trajectory_rows(pieces: list[Vector]) -> str:
    """trajectories.csv's text: each piece from START_NS, every SAMPLE_NS."""
    lines = ["case,time_ns," + ",".join(STATE_NAMES) + "\n"]
    times = [START_NS + sample * SAMPLE_NS for sample in range(SAMPLES)]
    for case, ((model, _), piece) in enumerate(zip(CASES, pieces, strict=True)):
        path = path_of(piece, model, times)
        lines.extend(numbers([case, time, *named(row, model)]) + "\n" for time, row in zip(times, path, strict=True))
    return "".join(lines)


def models_by_name() -> dict[str, FitModel]:
    """The candidate models by name."""
    return {model.name: model for model in (BALLISTIC, FLUTTER, LIFT)}


def fit_rows(case: int, entries: Iterable[Scored]) -> tuple[list[str], list[str]]:
    """One case's fits.csv and covariances.csv rows."""
    fits: list[str] = []
    covariances: list[str] = []
    models = models_by_name()
    for entry in entries:
        model, result = models[entry.name], entry.result
        deviations = np.sqrt(np.diag(result.covariance))
        outcome = [result.iterations, int(result.converged), result.residual_rms_m, entry.bic]
        state, sigma = named(result.state, model), named(deviations, model)
        fits.append(numbers([case, entry.name, result.epoch_ns, *outcome, *state, *sigma]) + "\n")
        size = model.law.size
        covariances.extend(
            numbers([case, entry.name, row, column, float(result.covariance[row, column])]) + "\n"
            for row in range(size)
            for column in range(row, size)
        )
    return fits, covariances


def fit_files(pieces: list[Vector], rng: np.random.Generator) -> tuple[str, str, str, str]:
    """measurements.csv's, fits.csv's, covariances.csv's and selection.csv's text."""
    points = ["case,time_ns,east,north,up\n"]
    header = ["case", "model", "epoch_ns", "iterations", "converged", "residual_rms_m", "bic"]
    fits = [",".join([*header, *STATE_NAMES, *(f"sigma_{name}" for name in STATE_NAMES)]) + "\n"]
    covariances = ["case,model,row,column,value\n"]
    selections = ["case,true_model,chosen\n"]
    times = [START_NS + point * POINT_NS for point in range(POINTS)]
    for case, ((model, _), piece) in enumerate(zip(CASES, pieces, strict=True)):
        positions = path_of(piece, model, times)[:, 0:3]
        positions = positions + rng.normal(0.0, SETTINGS.sigma_m, positions.shape)
        points.extend(numbers([case, t, *(float(v) for v in p)]) + "\n" for t, p in zip(times, positions, strict=True))
        selection: Selection = select(times, positions, SETTINGS)
        case_fits, case_covariances = fit_rows(case, selection.scored)
        fits.extend(case_fits)
        covariances.extend(case_covariances)
        selections.append(numbers([case, model.name, str(selection.chosen)]) + "\n")
    return "".join(points), "".join(fits), "".join(covariances), "".join(selections)


def write(directory: Path) -> None:
    """Write every golden file into directory."""
    directory.mkdir(parents=True, exist_ok=True)
    rng = np.random.default_rng(SEED)
    pieces = truths(rng)
    measurements, fits, covariances, selections = fit_files(pieces, rng)
    (directory / "parameters.csv").write_text(parameter_rows())
    (directory / "truths.csv").write_text(truth_rows(pieces))
    (directory / "trajectories.csv").write_text(trajectory_rows(pieces))
    (directory / "measurements.csv").write_text(measurements)
    (directory / "fits.csv").write_text(fits)
    (directory / "covariances.csv").write_text(covariances)
    (directory / "selection.csv").write_text(selections)


def main(argv: list[str]) -> int:
    """Write the golden files into the directory named."""
    if len(argv) != 1:
        sys.stderr.write("usage: python -m ics_golden.aero DIRECTORY\n")
        return EXIT_USAGE
    write(Path(argv[0]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
