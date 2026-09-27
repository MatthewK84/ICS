"""Tests for the ics_lint command line."""

from __future__ import annotations

import runpy
import sys
from pathlib import Path
from typing import Final

import pytest

from ics_lint.__main__ import main

EXIT_CLEAN: Final = 0
EXIT_FINDINGS: Final = 1
EXIT_UNREADABLE: Final = 2
CLEAN: Final = "def ok() -> int:\n    return 1\n"
RECURSIVE: Final = "def again() -> None:\n    again()\n"


def write(folder: Path, name: str, text: str) -> Path:
    path = folder / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
    return path


def test_clean_folder_passes(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    write(tmp_path, "pkg/clean.py", CLEAN)
    assert main([str(tmp_path)]) == EXIT_CLEAN
    assert capsys.readouterr().out == "ics_lint: 0 finding(s)\n"


def test_findings_fail_and_are_listed(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    path = write(tmp_path, "bad.py", RECURSIVE)
    assert main([str(path)]) == EXIT_FINDINGS
    assert f"{path}:1: ICS101 function 'again' is recursive" in capsys.readouterr().out


def test_hidden_and_excluded_folders_are_skipped(tmp_path: Path) -> None:
    write(tmp_path, ".venv/lib/bad.py", RECURSIVE)
    write(tmp_path, "seeded/bad.py", RECURSIVE)
    write(tmp_path, "clean.py", CLEAN)
    assert main([str(tmp_path), "--exclude", str(tmp_path / "seeded")]) == EXIT_CLEAN
    assert main([str(tmp_path)]) == EXIT_FINDINGS


def test_missing_path_is_an_error(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    assert main([str(tmp_path / "absent")]) == EXIT_UNREADABLE
    assert "no such file or folder" in capsys.readouterr().err


def test_unreadable_file_is_an_error(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    path = tmp_path / "binary.py"
    path.write_bytes(b"\xff\xfe\x00")
    assert main([str(path)]) == EXIT_UNREADABLE
    assert "cannot read" in capsys.readouterr().err


def test_runs_as_a_module(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    # Run __main__ afresh, as "python -m ics_lint" does, not the copy the tests imported.
    monkeypatch.delitem(sys.modules, "ics_lint.__main__", raising=False)
    monkeypatch.setattr(sys, "argv", ["ics_lint", str(write(tmp_path, "clean.py", CLEAN))])
    with pytest.raises(SystemExit) as exit_info:
        runpy.run_module("ics_lint", run_name="__main__")
    assert exit_info.value.code == EXIT_CLEAN
