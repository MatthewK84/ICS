"""golden/ballistic is what the ballistic reference writes (ICS-044).

Every number must be within 1e-12 of the golden one, or of 1 if larger, and
everything else the same text: numpy's matrix products round by CPU (see
test_cue_golden.py).
"""

import math
from pathlib import Path

import pytest

from ics_golden.ballistic import EXIT_USAGE, main

GOLDEN = Path(__file__).resolve().parents[2] / "golden" / "ballistic"
FILES: tuple[str, ...] = ("parameters.csv", "atmosphere.csv", "trajectories.csv", "measurements.csv", "fits.csv")
RELATIVE: float = 1e-12


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
    directory = tmp_path_factory.mktemp("ballistic")
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


def test_every_golden_fit_converged_and_found_kappa_within_3_sigma() -> None:
    lines = (GOLDEN / "fits.csv").read_text().splitlines()
    header = lines[0].split(",")
    for line in lines[1:]:
        row = dict(zip(header, line.split(","), strict=True))
        assert row["converged"] == "1"
        error = float(row["fit_kappa"]) - float(row["true_kappa"])
        assert abs(error) <= 3.0 * math.sqrt(float(row["cov_6_6"]))


def test_usage_is_shown_without_one_directory(capsys: pytest.CaptureFixture[str]) -> None:
    assert main([]) == EXIT_USAGE
    assert "usage" in capsys.readouterr().err
