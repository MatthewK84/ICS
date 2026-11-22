"""golden/aero is what the lift, flutter and selection reference writes (ICS-045).

Every number must be within 1e-12 of the golden one, or of 1 if larger, and
everything else the same text: numpy's matrix products round by CPU (see
test_cue_golden.py).
"""

import math
from pathlib import Path

import pytest

from ics_golden.aero import EXIT_USAGE, main

GOLDEN = Path(__file__).resolve().parents[2] / "golden" / "aero"
FILES: tuple[str, ...] = (
    "parameters.csv",
    "truths.csv",
    "trajectories.csv",
    "measurements.csv",
    "fits.csv",
    "covariances.csv",
    "selection.csv",
)
RELATIVE: float = 1e-12
PARAMETERS: tuple[str, ...] = ("kappa", "kappa_slope", "lift_kappa", "side_kappa")


def same_field(written: str, golden: str) -> bool:
    """Whether a field is the golden one: a number to RELATIVE, anything else exactly."""
    try:
        written_value = float(written)
        golden_value = float(golden)
    except ValueError:
        return written == golden
    return math.isclose(written_value, golden_value, rel_tol=RELATIVE, abs_tol=RELATIVE)


def fields_of(path: Path) -> list[list[str]]:
    """A file's lines, as fields split on commas and spaces."""
    return [line.replace(",", " ").split() for line in path.read_text().splitlines()]


@pytest.fixture(scope="module")
def written_directory(tmp_path_factory: pytest.TempPathFactory) -> Path:
    directory = tmp_path_factory.mktemp("aero")
    assert main([str(directory)]) == 0
    return directory


@pytest.mark.parametrize("name", FILES)
def test_golden_files_are_the_references_output(written_directory: Path, name: str) -> None:
    written, golden = fields_of(written_directory / name), fields_of(GOLDEN / name)
    assert len(written) == len(golden)
    for line, (written_row, golden_row) in enumerate(zip(written, golden, strict=True)):
        assert len(written_row) == len(golden_row), (name, line)
        assert all(same_field(*pair) for pair in zip(written_row, golden_row, strict=True)), (name, line)


def test_a_changed_number_is_found() -> None:
    assert same_field("0.1", "0.10000000000000002")
    assert not same_field("0.1", "0.1000001")
    assert not same_field("dark", "bright")


def rows_of(name: str) -> list[dict[str, str]]:
    """A golden file's rows, by column name."""
    lines = (GOLDEN / name).read_text().splitlines()
    header = lines[0].split(",")
    return [dict(zip(header, line.split(","), strict=True)) for line in lines[1:]]


def test_every_piece_is_given_its_own_model() -> None:
    selections = rows_of("selection.csv")
    assert len(selections) == len(rows_of("truths.csv"))
    assert all(row["chosen"] == row["true_model"] for row in selections)


def test_every_true_model_fit_converged_and_recovered_its_parameters_within_3_sigma() -> None:
    truths = {row["case"]: row for row in rows_of("truths.csv")}
    fits = [row for row in rows_of("fits.csv") if row["model"] == truths[row["case"]]["model"]]
    assert len(fits) == len(truths)
    for row in fits:
        assert row["converged"] == "1"
        for name in PARAMETERS:
            error = float(row[name]) - float(truths[row["case"]][name])
            assert abs(error) <= 3.0 * float(row[f"sigma_{name}"]), (row["case"], name)


def test_usage_is_shown_without_one_directory(capsys: pytest.CaptureFixture[str]) -> None:
    assert main([]) == EXIT_USAGE
    assert "usage" in capsys.readouterr().err
