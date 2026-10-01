"""Gate C++ coverage and assertion density (ICS-015, cpp/policy/check-dynamic.sh coverage).

  cpp_coverage.py binaries < CTEST_JSON
      prints the test executables named in `ctest --show-only=json-v1` output
  cpp_coverage.py check EXPORT --root ROOT --path-map RECORDED=REPO (--gates FILE | --gate PATH FLOOR ...)
      gates the code in an `llvm-cov export -format=text` file

Each gate names a path under the repository root and a floor. Test and fuzz
folders (any path with a test/ or fuzz/ component) are never gated: fuzz
targets run under libFuzzer, not ctest (ICS-016). For each gate:

- every line and every branch of its code must be covered, and every .cpp
  file under it must be in the export, that is, linked into some test;
- its assertion density must not fall below the floor. The density is the
  number of ics::check calls in its functions longer than three lines,
  divided by the number of those functions, truncated to two decimals.
  Shorter functions, such as accessors, are left out.

The floor only ratchets up: when a density rises above its floor, the check
passes and prints a notice to raise the floor. Function extents come from
llvm-cov, so no C++ is parsed here; calls are found in the source text, with
// comments removed. RECORDED=REPO maps the paths the compiler recorded
(cpp/cmake/Reproducible.cmake rewrites them) back to repository paths.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from collections.abc import Sequence
from dataclasses import dataclass
from decimal import Decimal, InvalidOperation
from pathlib import Path, PurePosixPath

JsonObject = dict[str, object]

CHECK_CALL: re.Pattern[str] = re.compile(r"(?<![\w.>:])(?:ics::)?check\(")
MAX_TRIVIAL_LINES: int = 3
HUNDRED: int = 100
MAX_FLOOR: Decimal = Decimal(100)
UNGATED_FOLDERS: frozenset[str] = frozenset({"test", "fuzz"})
GATE_FIELDS: int = 2
REGION_FIELDS: int = 6
FILE_ID: int = 5


class CoverageError(Exception):
    """An input that cannot be read or makes no sense."""


@dataclass(frozen=True)
class Gate:
    path: str
    floor_hundredths: int

    def covers(self, path: str) -> bool:
        if UNGATED_FOLDERS.intersection(PurePosixPath(path).parts):
            return False
        return path == self.path or path.startswith(self.path + "/")

    def floor(self) -> str:
        return f"{self.floor_hundredths / HUNDRED:.2f}"


@dataclass(frozen=True)
class PathMap:
    recorded: str
    repo: str

    def to_repo(self, recorded: str) -> str | None:
        """The repository path of a recorded file, or None for one outside the repository."""
        prefix = self.recorded.rstrip("/") + "/"
        if not recorded.startswith(prefix):
            return None
        return f"{self.repo.rstrip('/')}/{recorded[len(prefix) :]}"


@dataclass(frozen=True)
class FileCoverage:
    path: str
    lines_covered: int
    lines: int
    branches_covered: int
    branches: int


@dataclass(frozen=True, order=True)
class Function:
    path: str
    start_line: int
    start_column: int
    end_line: int
    end_column: int

    def trivial(self) -> bool:
        return self.end_line - self.start_line + 1 <= MAX_TRIVIAL_LINES

    def contains(self, line: int, column: int) -> bool:
        return (self.start_line, self.start_column) <= (line, column) <= (self.end_line, self.end_column)

    def span(self) -> tuple[int, int]:
        return (self.end_line - self.start_line, self.end_column - self.start_column)


@dataclass(frozen=True)
class Density:
    checks: int
    functions: int

    def hundredths(self) -> int:
        return self.checks * HUNDRED // self.functions if self.functions else 0

    def text(self) -> str:
        return f"{self.hundredths() / HUNDRED:.2f}"


@dataclass(frozen=True)
class Report:
    lines: list[str]
    errors: list[str]
    notices: list[str]


def objects(value: object) -> list[JsonObject]:
    """The JSON objects in a list; anything else counts as empty."""
    if not isinstance(value, list):
        return []
    return [item for item in value if isinstance(item, dict)]


def number(data: JsonObject, *keys: str) -> int:
    """An integer found by following keys through nested objects, or 0."""
    current: object = data
    for key in keys:
        if not isinstance(current, dict):
            return 0
        current = current.get(key)
    return current if isinstance(current, int) else 0


def parse_floor(text: str) -> int:
    """A floor such as "0.53", in hundredths."""
    try:
        floor = Decimal(text)
    except InvalidOperation as error:
        raise CoverageError(f"floor '{text}' is not a number") from error
    hundredths = floor * HUNDRED
    if not floor.is_finite() or not Decimal(0) <= floor <= MAX_FLOOR or hundredths != hundredths.to_integral_value():
        raise CoverageError(f"floor '{text}' must be between 0 and 100, with at most two decimals")
    return int(hundredths)


def parse_gates(pairs: Sequence[tuple[str, str]]) -> list[Gate]:
    gates = [Gate(path.rstrip("/"), parse_floor(floor)) for path, floor in pairs]
    if not gates:
        raise CoverageError("no gates")
    paths = [gate.path for gate in gates]
    if len(set(paths)) != len(paths):
        raise CoverageError("a path is gated twice")
    return gates


def gate_pairs(text: str) -> list[tuple[str, str]]:
    """The '<path> <floor>' pairs in a gates file, skipping blank lines and # comments."""
    pairs: list[tuple[str, str]] = []
    for line_number, raw in enumerate(text.splitlines(), start=1):
        fields = raw.split("#", 1)[0].split()
        if not fields:
            continue
        if len(fields) != GATE_FIELDS:
            raise CoverageError(f"gates line {line_number}: expected '<path> <floor>', got '{raw.strip()}'")
        pairs.append((fields[0], fields[1]))
    return pairs


def parse_path_map(text: str) -> PathMap:
    recorded, separator, repo = text.partition("=")
    if not separator or not recorded or not repo:
        raise CoverageError(f"--path-map must be RECORDED=REPO, got '{text}'")
    return PathMap(recorded, repo)


def export_data(document: object) -> JsonObject:
    if not isinstance(document, dict):
        raise CoverageError("an llvm-cov export must hold a JSON object")
    data = objects(document.get("data"))
    if len(data) != 1:
        raise CoverageError("an llvm-cov export must hold exactly one data object")
    return data[0]


def file_coverage(data: JsonObject, paths: PathMap) -> list[FileCoverage]:
    found: list[FileCoverage] = []
    for entry in objects(data.get("files")):
        filename = entry.get("filename")
        path = paths.to_repo(filename) if isinstance(filename, str) else None
        if path is None:
            continue
        lines = (number(entry, "summary", "lines", "covered"), number(entry, "summary", "lines", "count"))
        branches = (number(entry, "summary", "branches", "covered"), number(entry, "summary", "branches", "count"))
        found.append(FileCoverage(path, *lines, *branches))
    return found


def function_extent(entry: JsonObject, paths: PathMap) -> Function | None:
    """A function's body: the first region of its record, in its own file."""
    regions = objects_or_lists(entry.get("regions"))
    filenames = entry.get("filenames")
    if not regions or not isinstance(filenames, list):
        return None
    fields = [value for value in regions[0][:REGION_FIELDS] if isinstance(value, int)]
    if len(fields) != REGION_FIELDS or fields[FILE_ID] >= len(filenames):
        return None
    filename = filenames[fields[FILE_ID]]
    path = paths.to_repo(filename) if isinstance(filename, str) else None
    if path is None:
        return None
    return Function(path, fields[0], fields[1], fields[2], fields[3])


def objects_or_lists(value: object) -> list[list[object]]:
    """The lists in a list; anything else counts as empty."""
    if not isinstance(value, list):
        return []
    return [item for item in value if isinstance(item, list)]


def functions(data: JsonObject, paths: PathMap) -> list[Function]:
    """Each function once: the instantiations of a template share one extent."""
    found: set[Function] = set()
    for entry in objects(data.get("functions")):
        extent = function_extent(entry, paths)
        if extent is not None:
            found.add(extent)
    return sorted(found)


def check_positions(source: str) -> list[tuple[int, int]]:
    """The 1-based (line, column) of each ics::check call, ignoring // comments."""
    positions: list[tuple[int, int]] = []
    for line_number, line in enumerate(source.splitlines(), start=1):
        code = line.split("//", 1)[0]
        positions.extend((line_number, match.start() + 1) for match in CHECK_CALL.finditer(code))
    return positions


def innermost(candidates: Sequence[Function], line: int, column: int) -> Function | None:
    containing = [function for function in candidates if function.contains(line, column)]
    return min(containing, key=Function.span) if containing else None


def count_checks(extents: Sequence[Function], sources: dict[str, str]) -> dict[Function, int]:
    """The ics::check calls in each function, excluding those in nested lambdas."""
    counts: dict[Function, int] = dict.fromkeys(extents, 0)
    for path, source in sources.items():
        in_file = [function for function in extents if function.path == path]
        for line, column in check_positions(source):
            owner = innermost(in_file, line, column)
            if owner is not None:
                counts[owner] += 1
    return counts


def read_sources(root: Path, paths: set[str]) -> dict[str, str]:
    sources: dict[str, str] = {}
    for path in sorted(paths):
        try:
            sources[path] = (root / path).read_text(encoding="utf-8")
        except OSError as error:
            raise CoverageError(f"cannot read {path}: {error}") from error
    return sources


def cpp_files(gate: Gate, root: Path) -> list[str]:
    """The gated .cpp files on disk."""
    base = root / gate.path
    if not base.exists():
        raise CoverageError(f"gated path {gate.path} does not exist")
    candidates = [base] if base.is_file() else sorted(base.rglob("*.cpp"))
    relative = [candidate.relative_to(root).as_posix() for candidate in candidates if candidate.suffix == ".cpp"]
    return [path for path in relative if gate.covers(path)]


def coverage_report(gate: Gate, files: Sequence[FileCoverage], root: Path) -> Report:
    gated = [entry for entry in files if gate.covers(entry.path)]
    errors = [
        f"{entry.path}: lines {entry.lines_covered} of {entry.lines} covered, "
        f"branches {entry.branches_covered} of {entry.branches} covered"
        for entry in gated
        if entry.lines_covered < entry.lines or entry.branches_covered < entry.branches
    ]
    exported = {entry.path for entry in gated}
    errors.extend(
        f"{path}: not linked into any test, so none of it is covered"
        for path in cpp_files(gate, root)
        if path not in exported
    )
    lines = (sum(entry.lines_covered for entry in gated), sum(entry.lines for entry in gated))
    branches = (sum(entry.branches_covered for entry in gated), sum(entry.branches for entry in gated))
    summary = f"{gate.path}: lines {lines[0]} of {lines[1]} covered, branches {branches[0]} of {branches[1]} covered"
    return Report([summary], errors, [])


def density_report(gate: Gate, counts: dict[Function, int]) -> Report:
    measured = {function: checks for function, checks in counts.items() if gate.covers(function.path)}
    counted = {function: checks for function, checks in measured.items() if not function.trivial()}
    density = Density(sum(counted.values()), len(counted))
    detail = f"{density.checks} checks in {density.functions} functions longer than {MAX_TRIVIAL_LINES} lines"
    summary = f"{gate.path}: assertion density {density.text()} ({detail}); floor {gate.floor()}"
    if density.functions and density.checks * HUNDRED < gate.floor_hundredths * density.functions:
        unchecked = [f"  {f.path}:{f.start_line} has no check" for f, checks in sorted(counted.items()) if not checks]
        error = f"{gate.path}: assertion density {density.text()} is below the floor {gate.floor()} ({detail})"
        return Report([summary, *unchecked], [error], [])
    if density.hundredths() > gate.floor_hundredths:
        above = f"{gate.path}: assertion density {density.text()} is above its floor {gate.floor()}"
        return Report([summary], [], [f"{above}; raise the floor"])
    return Report([summary], [], [])


def evaluate(gates: Sequence[Gate], data: JsonObject, paths: PathMap, root: Path) -> list[Report]:
    files = file_coverage(data, paths)
    extents = [function for function in functions(data, paths) if any(gate.covers(function.path) for gate in gates)]
    counts = count_checks(extents, read_sources(root, {function.path for function in extents}))
    reports: list[Report] = []
    for gate in gates:
        reports.append(coverage_report(gate, files, root))
        reports.append(density_report(gate, counts))
    return reports


def print_reports(reports: Sequence[Report]) -> int:
    errors = [error for report in reports for error in report.errors]
    for report in reports:
        for line in report.lines:
            sys.stdout.write(line + "\n")
        for notice in report.notices:
            sys.stdout.write(f"::notice::{notice}\n")
    for error in errors:
        sys.stdout.write(f"::error::{error}\n")
    sys.stdout.write(f"cpp_coverage: {len(errors)} failure(s)\n")
    return 1 if errors else 0


def load_json(text: str, what: str) -> object:
    try:
        document: object = json.loads(text)
    except json.JSONDecodeError as error:
        raise CoverageError(f"{what} is not JSON: {error}") from error
    return document


def read_text(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8")
    except OSError as error:
        raise CoverageError(f"cannot read {path}: {error}") from error


def test_binaries(document: object) -> list[str]:
    """The executables of the tests in `ctest --show-only=json-v1` output."""
    if not isinstance(document, dict):
        raise CoverageError("ctest JSON must hold an object")
    found: set[str] = set()
    for test in objects(document.get("tests")):
        command = test.get("command")
        if isinstance(command, list) and command and isinstance(command[0], str):
            found.add(command[0])
    if not found:
        raise CoverageError("ctest lists no tests")
    return sorted(found)


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(prog="cpp_coverage", description="Gate C++ coverage and assertion density.")
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("binaries", help="print the test executables in ctest JSON read from stdin")
    check = commands.add_parser("check", help="gate an llvm-cov export")
    check.add_argument("export", type=Path, help="llvm-cov export -format=text output")
    check.add_argument("--root", type=Path, required=True, help="the repository root")
    check.add_argument("--path-map", required=True, metavar="RECORDED=REPO", help="recorded path prefix to repo path")
    source = check.add_mutually_exclusive_group(required=True)
    source.add_argument("--gates", type=Path, metavar="FILE", help="a file of '<path> <floor>' lines")
    source.add_argument("--gate", nargs=GATE_FIELDS, action="append", metavar=("PATH", "FLOOR"), help="one gate")
    return parser.parse_args(argv)


def run_check(args: argparse.Namespace) -> int:
    pairs = gate_pairs(read_text(args.gates)) if args.gates else [(path, floor) for path, floor in args.gate]
    gates = parse_gates(pairs)
    data = export_data(load_json(read_text(args.export), str(args.export)))
    return print_reports(evaluate(gates, data, parse_path_map(args.path_map), args.root))


def main(argv: Sequence[str]) -> int:
    args = parse_args(argv)
    try:
        if args.command == "binaries":
            for binary in test_binaries(load_json(sys.stdin.read(), "stdin")):
                sys.stdout.write(binary + "\n")
            return 0
        return run_check(args)
    except CoverageError as error:
        sys.stdout.write(f"::error::{error}\n")
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
