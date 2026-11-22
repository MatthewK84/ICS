"""golden/imm is what the IMM and closest-approach references write (ICS-046).

Every number must be within 1e-12 of the golden one, or of 1 if larger, and
everything else the same text: numpy's matrix products round by CPU (see
test_cue_golden.py). The tracks and their encounters carry that rounding
through 500 filter updates, so they are held to TRACKED instead: OpenBLAS's
Haswell kernels move them by up to 4e-11 from the SkylakeX ones that wrote
the files.
"""

import math
from pathlib import Path

import pytest

from ics_golden.imm import EXIT_USAGE, TRUE_MISS_M, main

GOLDEN = Path(__file__).resolve().parents[2] / "golden" / "imm"
FILES: tuple[str, ...] = (
    "parameters.csv",
    "motion.csv",
    "measurements.csv",
    "tracks.csv",
    "encounters.csv",
    "approaches.csv",
)
RELATIVE: float = 1e-12
TRACKED: float = 1e-9
TRACKED_FILES: frozenset[str] = frozenset({"tracks.csv", "encounters.csv"})


def same_field(written: str, golden: str, relative: float = RELATIVE) -> bool:
    """Whether a field is the golden one: a number to relative, anything else exactly."""
    try:
        written_value = float(written)
        golden_value = float(golden)
    except ValueError:
        return written == golden
    return math.isclose(written_value, golden_value, rel_tol=relative, abs_tol=relative)


def fields_of(path: Path) -> list[list[str]]:
    """A file's lines, as fields split on commas and spaces."""
    return [line.replace(",", " ").split() for line in path.read_text().splitlines()]


@pytest.fixture(scope="module")
def written_directory(tmp_path_factory: pytest.TempPathFactory) -> Path:
    directory = tmp_path_factory.mktemp("imm")
    assert main([str(directory)]) == 0
    return directory


@pytest.mark.parametrize("name", FILES)
def test_golden_files_are_the_references_output(written_directory: Path, name: str) -> None:
    written, golden = fields_of(written_directory / name), fields_of(GOLDEN / name)
    assert len(written) == len(golden)
    for line, (written_row, golden_row) in enumerate(zip(written, golden, strict=True)):
        assert len(written_row) == len(golden_row), (name, line)
        relative = TRACKED if name in TRACKED_FILES else RELATIVE
        pairs = zip(written_row, golden_row, strict=True)
        assert all(same_field(first, second, relative) for first, second in pairs), (name, line)


def test_a_changed_number_is_found() -> None:
    assert same_field("0.1", "0.10000000000000002")
    assert not same_field("0.1", "0.1000001")
    assert not same_field("dark", "bright")
    assert same_field("2.4079972229131443", "2.4079972228166198", TRACKED)
    assert not same_field("2.4079972229131443", "2.4079972228166198")


def test_the_golden_engagement_ends_within_3_sigma_of_the_true_miss() -> None:
    last = (GOLDEN / "encounters.csv").read_text().splitlines()[-1].split(",")
    assert last[1] == "1"
    assert abs(float(last[3]) - TRUE_MISS_M) <= 3.0 * float(last[4])


def test_usage_is_shown_without_one_directory(capsys: pytest.CaptureFixture[str]) -> None:
    assert main([]) == EXIT_USAGE
    assert "usage" in capsys.readouterr().err
