"""golden/cue is what the cue reference writes (ICS-037)."""

from pathlib import Path

import pytest

from ics_golden.cue import EXIT_USAGE, main, write

GOLDEN = Path(__file__).resolve().parents[2] / "golden" / "cue"
FILES: tuple[str, ...] = ("parameters.csv", "measurements.csv", "reports.csv", "queries.csv", "cues.csv")


@pytest.mark.parametrize("name", FILES)
def test_golden_files_are_the_references_output(tmp_path: Path, name: str) -> None:
    write(tmp_path)
    assert (tmp_path / name).read_text() == (GOLDEN / name).read_text()


def test_reports_cover_every_outcome() -> None:
    outcomes = {line.split(",")[11] for line in (GOLDEN / "reports.csv").read_text().splitlines()[1:]}
    assert outcomes == {"taken", "late", "gated", "too_old"}


def test_usage(capsys: pytest.CaptureFixture[str]) -> None:
    assert main([]) == EXIT_USAGE
    assert "usage" in capsys.readouterr().err
