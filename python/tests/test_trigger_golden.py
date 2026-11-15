"""golden/trigger is what the trigger reference writes (ICS-040).

Every number must be within 1e-12 of the golden one, or of 1 if larger, and
everything else the same text: numpy's matrix products round by CPU (see
test_cue_golden.py).
"""

import math
from pathlib import Path

import pytest

from ics_golden.trigger import EXIT_USAGE, main, write

GOLDEN = Path(__file__).resolve().parents[2] / "golden" / "trigger"
FILES: tuple[str, ...] = ("parameters.csv", "approaches.csv", "shots.csv")
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


@pytest.mark.parametrize("name", FILES)
def test_golden_files_are_the_references_output(tmp_path: Path, name: str) -> None:
    write(tmp_path)
    written, golden = fields_of(tmp_path / name), fields_of(GOLDEN / name)
    assert len(written) == len(golden)
    for line, (written_row, golden_row) in enumerate(zip(written, golden, strict=True)):
        assert len(written_row) == len(golden_row), (name, line)
        assert all(same_field(*pair) for pair in zip(written_row, golden_row, strict=True)), (name, line)


def test_a_changed_number_is_found() -> None:
    assert same_field("0.1", "0.10000000000000002")
    assert not same_field("0.1", "0.1000001")
    assert not same_field("dark", "bright")


def test_the_cases_find_approaches_and_find_none() -> None:
    outcomes = {line.split(",")[19] for line in (GOLDEN / "approaches.csv").read_text().splitlines()[1:]}
    assert outcomes == {"0", "1"}


def test_the_shots_are_on_time_and_late_and_their_windows_open_and_shut() -> None:
    rows = [line.split(",") for line in (GOLDEN / "shots.csv").read_text().splitlines()[1:]]
    assert {(row[4], row[5]) for row in rows} == {("0", "0"), ("0", "1"), ("1", "0"), ("1", "1")}


def test_usage(capsys: pytest.CaptureFixture[str]) -> None:
    assert main([]) == EXIT_USAGE
    assert "usage" in capsys.readouterr().err


def test_writes_into_a_directory(tmp_path: Path) -> None:
    assert main([str(tmp_path)]) == 0
    assert sorted(path.name for path in tmp_path.iterdir()) == sorted(path.name for path in GOLDEN.iterdir())
