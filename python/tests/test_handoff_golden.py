"""golden/handoff is what the handoff reference writes (ICS-039).

Every number must be within 1e-12 of the golden one, or of 1 if larger, and
everything else the same text: numpy's matrix products round by CPU (see
test_cue_golden.py).
"""

import math
from pathlib import Path

import pytest

from ics_golden.handoff import EXIT_USAGE, main, write

GOLDEN = Path(__file__).resolve().parents[2] / "golden" / "handoff"
FILES: tuple[str, ...] = ("parameters.csv", "approaches.csv", "slews.csv", "reaches.csv", "queue.csv")
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
    outcomes = {line.split(",")[13] for line in (GOLDEN / "approaches.csv").read_text().splitlines()[1:]}
    assert outcomes == {"0", "1"}


def test_the_run_ends_with_every_engagement_done_and_three_flagged() -> None:
    last = [line.split(",") for line in (GOLDEN / "queue.csv").read_text().splitlines()[-4:]]
    assert [row[9] for row in last] == ["1", "1", "1", "1"]
    assert [row[13] + row[14] for row in last] == ["00", "11", "10", "00"]
    assert {row[16] for row in last} == {"-1"}


def test_usage(capsys: pytest.CaptureFixture[str]) -> None:
    assert main([]) == EXIT_USAGE
    assert "usage" in capsys.readouterr().err


def test_writes_into_a_directory(tmp_path: Path) -> None:
    assert main([str(tmp_path)]) == 0
    assert sorted(path.name for path in tmp_path.iterdir()) == sorted(path.name for path in GOLDEN.iterdir())
