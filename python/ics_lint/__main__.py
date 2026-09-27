"""Command line for ics_lint: ``python -m ics_lint PATH... [--exclude DIR]``.

Exit status: 0 when clean, 1 when there are findings, 2 when a path cannot be read.
"""

from __future__ import annotations

import argparse
import sys
from collections.abc import Sequence
from pathlib import Path

from ics_lint.checks import Finding, check_source


class LintError(Exception):
    """A path that cannot be linted, such as a missing or unreadable file."""

    @classmethod
    def missing(cls, path: Path) -> LintError:
        return cls(f"no such file or folder: {path}")

    @classmethod
    def unreadable(cls, path: Path, error: Exception) -> LintError:
        return cls(f"cannot read {path}: {error}")


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(prog="ics_lint", description="Power-of-Ten checks for ICS Python code.")
    parser.add_argument("paths", nargs="+", type=Path, help="files or folders to check")
    parser.add_argument("--exclude", action="append", type=Path, default=[], help="folder to skip; repeatable")
    return parser.parse_args(argv)


def is_skipped(path: Path, excluded: Sequence[Path]) -> bool:
    """Skip hidden folders such as .venv, and anything under an excluded folder."""
    if any(part.startswith(".") and part not in {".", ".."} for part in path.parts):
        return True
    resolved = path.resolve()
    return any(resolved.is_relative_to(folder.resolve()) for folder in excluded)


def python_files(paths: Sequence[Path], excluded: Sequence[Path]) -> list[Path]:
    files: list[Path] = []
    for path in paths:
        if path.is_file():
            files.append(path)
        elif path.is_dir():
            files.extend(found for found in sorted(path.rglob("*.py")) if not is_skipped(found, excluded))
        else:
            raise LintError.missing(path)
    return files


def lint(paths: Sequence[Path], excluded: Sequence[Path]) -> list[Finding]:
    findings: list[Finding] = []
    for path in python_files(paths, excluded):
        try:
            source = path.read_text(encoding="utf-8")
        except (OSError, UnicodeDecodeError) as error:
            raise LintError.unreadable(path, error) from error
        findings.extend(check_source(source, str(path)))
    return findings


def main(argv: Sequence[str]) -> int:
    args = parse_args(argv)
    try:
        findings = lint(args.paths, args.exclude)
    except LintError as error:
        sys.stderr.write(f"ics_lint: {error}\n")
        return 2
    for finding in findings:
        sys.stdout.write(finding.render() + "\n")
    sys.stdout.write(f"ics_lint: {len(findings)} finding(s)\n")
    return 1 if findings else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
