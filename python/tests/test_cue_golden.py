"""golden/cue is what the cue reference writes (ICS-037).

Every number must be within 1e-12 of the golden one, or of 1 if larger, and
everything else must be the same text. The last bit of a number can differ
between machines: numpy's matrix products run in OpenBLAS, which picks its
kernels by CPU, and the kernels round differently.
"""

import math
from pathlib import Path

import pytest

from ics_golden.cue import EXIT_USAGE, main, write

GOLDEN = Path(__file__).resolve().parents[2] / "golden" / "cue"
FILES: tuple[str, ...] = ("parameters.csv", "measurements.csv", "reports.csv", "queries.csv", "cues.csv")
RELATIVE: float = 1e-12


def rows_of(path: Path) -> list[list[str]]:
    """A CSV file's lines, header first, as fields."""
    return [line.split(",") for line in path.read_text().splitlines()]


def same_field(written: str, golden: str) -> bool:
    """Whether a field is the golden one: a number to RELATIVE, anything else exactly."""
    try:
        written_value = float(written)
        golden_value = float(golden)
    except ValueError:
        return written == golden
    return math.isclose(written_value, golden_value, rel_tol=RELATIVE, abs_tol=RELATIVE)


def mismatches(written: list[str], golden: list[str]) -> list[int]:
    """The columns where a written line differs from the golden one."""
    return [column for column, pair in enumerate(zip(written, golden, strict=True)) if not same_field(*pair)]


@pytest.mark.parametrize("name", FILES)
def test_golden_files_are_the_references_output(tmp_path: Path, name: str) -> None:
    write(tmp_path)
    written = rows_of(tmp_path / name)
    golden = rows_of(GOLDEN / name)
    assert len(written) == len(golden)
    for line, (written_row, golden_row) in enumerate(zip(written, golden, strict=True)):
        assert len(written_row) == len(golden_row), (name, line)
        assert mismatches(written_row, golden_row) == [], (name, line)


def test_a_changed_number_is_found() -> None:
    assert same_field("0.1", "0.10000000000000002")
    assert not same_field("0.1", "0.1000001")
    assert not same_field("taken", "late")
    assert mismatches(["1", "late", "2.5"], ["1", "taken", "2.6"]) == [1, 2]


def test_reports_cover_every_outcome() -> None:
    outcomes = {line.split(",")[11] for line in (GOLDEN / "reports.csv").read_text().splitlines()[1:]}
    assert outcomes == {"taken", "late", "gated", "too_old"}


def test_usage(capsys: pytest.CaptureFixture[str]) -> None:
    assert main([]) == EXIT_USAGE
    assert "usage" in capsys.readouterr().err
