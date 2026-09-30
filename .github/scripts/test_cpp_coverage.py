"""Unit tests for cpp_coverage.py (stdlib unittest, no network)."""

from __future__ import annotations

import io
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import cpp_coverage as cc

EXIT_CLEAN: int = 0
EXIT_FAILED: int = 1
PATHS: cc.PathMap = cc.PathMap("build/src", "cpp")
LIBRARY_SOURCE: str = """\
namespace ics {
int f(int x) {
  if (!check(x > 0)) {  // a check(
    return 0;
  }
  return x;
}
int g(int x) {
  // check(x) in a comment does not count
  const auto h = [](int y) { return ics::check(y > 0); };
  static_cast<void>(h);
  return other::check(x) + a.check(x);
}
int size() { return 1; }
}
"""


def region(start: tuple[int, int], end: tuple[int, int], file_id: int = 0) -> list[int]:
    return [start[0], start[1], end[0], end[1], 1, file_id, 0, 0]


def function_entry(filename: str, start: tuple[int, int], end: tuple[int, int]) -> cc.JsonObject:
    return {"name": "f", "filenames": [filename], "regions": [region(start, end)]}


def file_entry(filename: str, lines: tuple[int, int], branches: tuple[int, int]) -> cc.JsonObject:
    summary = {
        "lines": {"covered": lines[0], "count": lines[1]},
        "branches": {"covered": branches[0], "count": branches[1]},
    }
    return {"filename": filename, "summary": summary}


def library_export(branches_covered: int = 2) -> cc.JsonObject:
    recorded = "build/src/lib/f.cpp"
    functions = [
        function_entry(recorded, (2, 14), (7, 1)),
        function_entry(recorded, (8, 14), (13, 1)),
        function_entry(recorded, (10, 28), (10, 56)),
        function_entry(recorded, (14, 12), (14, 23)),
        function_entry("/usr/include/c++/13/vector", (1, 1), (9, 1)),
    ]
    files = [file_entry(recorded, (12, 12), (branches_covered, 2)), file_entry("/usr/include/x.h", (0, 5), (0, 0))]
    return {"data": [{"files": files, "functions": functions}]}


def write_library(root: Path) -> None:
    (root / "cpp" / "lib").mkdir(parents=True)
    (root / "cpp" / "lib" / "f.cpp").write_text(LIBRARY_SOURCE, encoding="utf-8")


def run_main(argv: list[str], stdin: str = "") -> tuple[int, str]:
    with patch("sys.stdout", new_callable=io.StringIO) as out, patch("sys.stdin", io.StringIO(stdin)):
        code = cc.main(argv)
    return code, out.getvalue()


class GateTests(unittest.TestCase):
    def test_reads_gates_skipping_comments(self) -> None:
        pairs = cc.gate_pairs("# header\n\ncpp/common 0.53  # note\ncpp/other/ 1\n")
        self.assertEqual(cc.parse_gates(pairs), [cc.Gate("cpp/common", 53), cc.Gate("cpp/other", 100)])

    def test_rejects_a_malformed_gates_line(self) -> None:
        with self.assertRaisesRegex(cc.CoverageError, "gates line 1"):
            cc.gate_pairs("cpp/common\n")

    def test_rejects_bad_floors(self) -> None:
        for floor in ("x", "-1", "101", "0.555", "NaN"):
            with self.subTest(floor=floor), self.assertRaises(cc.CoverageError):
                cc.parse_floor(floor)
        self.assertEqual(cc.parse_floor("2"), 200)

    def test_rejects_no_gates_and_duplicates(self) -> None:
        with self.assertRaisesRegex(cc.CoverageError, "no gates"):
            cc.parse_gates([])
        with self.assertRaisesRegex(cc.CoverageError, "gated twice"):
            cc.parse_gates([("cpp/a", "1"), ("cpp/a/", "2")])

    def test_covers_its_path_but_never_tests(self) -> None:
        gate = cc.Gate("cpp/common", 0)
        self.assertTrue(gate.covers("cpp/common/src/a.cpp"))
        self.assertTrue(gate.covers("cpp/common"))
        self.assertFalse(gate.covers("cpp/common/test/a_test.cpp"))
        self.assertFalse(gate.covers("cpp/commonplace/a.cpp"))


class ExportTests(unittest.TestCase):
    def test_maps_recorded_paths(self) -> None:
        self.assertEqual(PATHS.to_repo("build/src/common/a.cpp"), "cpp/common/a.cpp")
        self.assertIsNone(PATHS.to_repo("/usr/include/vector"))
        self.assertEqual(cc.parse_path_map("build/src=cpp"), PATHS)
        with self.assertRaises(cc.CoverageError):
            cc.parse_path_map("build/src")

    def test_rejects_an_export_without_one_data_object(self) -> None:
        documents: tuple[object, ...] = ([], {"data": []}, {"data": [{}, {}]})
        for document in documents:
            with self.subTest(document=document), self.assertRaises(cc.CoverageError):
                cc.export_data(document)

    def test_reads_file_coverage_inside_the_repository(self) -> None:
        data = cc.export_data(library_export(branches_covered=1))
        self.assertEqual(cc.file_coverage(data, PATHS), [cc.FileCoverage("cpp/lib/f.cpp", 12, 12, 1, 2)])

    def test_reads_each_function_once(self) -> None:
        # A second instantiation of a template has the same extent as the first.
        duplicate = function_entry("build/src/lib/f.cpp", (2, 14), (7, 1))
        entries = [*cc.objects(cc.export_data(library_export()).get("functions")), duplicate]
        self.assertEqual(len(cc.functions({"functions": entries}, PATHS)), 4)

    def test_skips_malformed_function_records(self) -> None:
        malformed: list[cc.JsonObject] = [
            {"filenames": ["build/src/a.cpp"], "regions": []},
            {"filenames": "x", "regions": [region((1, 1), (5, 1))]},
            {"filenames": ["build/src/a.cpp"], "regions": [["1", 1, 5, 1, 1, 0]]},
            {"filenames": ["build/src/a.cpp"], "regions": [region((1, 1), (5, 1), file_id=3)]},
            {"filenames": [7], "regions": [region((1, 1), (5, 1))]},
        ]
        self.assertEqual(cc.functions({"functions": malformed}, PATHS), [])


class DensityTests(unittest.TestCase):
    def test_finds_check_calls_outside_comments(self) -> None:
        self.assertEqual(cc.check_positions(LIBRARY_SOURCE), [(3, 8), (10, 37)])

    def test_counts_each_call_in_its_innermost_function(self) -> None:
        extents = cc.functions(cc.export_data(library_export()), PATHS)
        counts = cc.count_checks(extents, {"cpp/lib/f.cpp": LIBRARY_SOURCE})
        self.assertEqual(sorted(counts.values()), [0, 0, 1, 1])
        lambda_extent = cc.Function("cpp/lib/f.cpp", 10, 28, 10, 56)
        self.assertEqual(counts[lambda_extent], 1)

    def test_truncates_density_to_hundredths(self) -> None:
        self.assertEqual(cc.Density(8, 15).text(), "0.53")
        self.assertEqual(cc.Density(0, 0).hundredths(), 0)

    def test_fails_below_the_floor_and_names_unchecked_functions(self) -> None:
        counts = {cc.Function("cpp/lib/a.cpp", 1, 1, 9, 1): 1, cc.Function("cpp/lib/a.cpp", 10, 1, 19, 1): 0}
        report = cc.density_report(cc.Gate("cpp/lib", 60), counts)
        self.assertEqual(len(report.errors), 1)
        self.assertIn("assertion density 0.50 is below the floor 0.60", report.errors[0])
        self.assertIn("  cpp/lib/a.cpp:10 has no check", report.lines)

    def test_asks_to_raise_a_floor_the_density_exceeds(self) -> None:
        counts = {cc.Function("cpp/lib/a.cpp", 1, 1, 9, 1): 2}
        report = cc.density_report(cc.Gate("cpp/lib", 150), counts)
        self.assertEqual(report.errors, [])
        self.assertEqual(report.notices, ["cpp/lib: assertion density 2.00 is above its floor 1.50; raise the floor"])

    def test_passes_a_gate_with_only_short_functions(self) -> None:
        counts = {cc.Function("cpp/lib/a.cpp", 1, 1, 3, 1): 0}
        report = cc.density_report(cc.Gate("cpp/lib", 100), counts)
        self.assertEqual((report.errors, report.notices), ([], []))


class CoverageTests(unittest.TestCase):
    def test_fails_uncovered_code_and_unlinked_sources(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            write_library(root)
            (root / "cpp" / "lib" / "unlinked.cpp").write_text("int u() { return 0; }\n", encoding="utf-8")
            (root / "cpp" / "lib" / "test").mkdir()
            (root / "cpp" / "lib" / "test" / "f_test.cpp").write_text("\n", encoding="utf-8")
            files = [cc.FileCoverage("cpp/lib/f.cpp", 12, 12, 1, 2)]
            report = cc.coverage_report(cc.Gate("cpp/lib", 0), files, root)
        self.assertEqual(
            report.errors,
            [
                "cpp/lib/f.cpp: lines 12 of 12 covered, branches 1 of 2 covered",
                "cpp/lib/unlinked.cpp: not linked into any test, so none of it is covered",
            ],
        )
        self.assertEqual(report.lines, ["cpp/lib: lines 12 of 12 covered, branches 1 of 2 covered"])

    def test_rejects_a_gated_path_that_does_not_exist(self) -> None:
        with tempfile.TemporaryDirectory() as folder, self.assertRaisesRegex(cc.CoverageError, "does not exist"):
            cc.coverage_report(cc.Gate("cpp/missing", 0), [], Path(folder))


class MainTests(unittest.TestCase):
    def run_check(self, export: cc.JsonObject, gates: str) -> tuple[int, str]:
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            write_library(root)
            (root / "export.json").write_text(json.dumps(export), encoding="utf-8")
            (root / "gates.txt").write_text(gates, encoding="utf-8")
            argv = ["check", str(root / "export.json"), "--root", str(root), "--path-map", "build/src=cpp"]
            return run_main([*argv, "--gates", str(root / "gates.txt")])

    def test_passes_covered_code_at_its_floor(self) -> None:
        code, out = self.run_check(library_export(), "cpp/lib 0.50\n")
        self.assertEqual(code, EXIT_CLEAN, out)
        self.assertIn("cpp/lib: assertion density 0.50 (1 checks in 2 functions longer than 3 lines)", out)
        self.assertIn("cpp_coverage: 0 failure(s)", out)

    def test_fails_uncovered_code(self) -> None:
        code, out = self.run_check(library_export(branches_covered=1), "cpp/lib 0.50\n")
        self.assertEqual(code, EXIT_FAILED)
        self.assertIn("::error::cpp/lib/f.cpp: lines 12 of 12 covered, branches 1 of 2 covered", out)

    def test_takes_gates_on_the_command_line(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            write_library(root)
            (root / "export.json").write_text(json.dumps(library_export()), encoding="utf-8")
            argv = ["check", str(root / "export.json"), "--root", str(root), "--path-map", "build/src=cpp"]
            code, out = run_main([*argv, "--gate", "cpp/lib", "1"])
        self.assertEqual(code, EXIT_FAILED)
        self.assertIn("::error::cpp/lib: assertion density 0.50 is below the floor 1.00", out)

    def test_reports_unreadable_input(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            argv = ["check", str(Path(folder) / "none.json"), "--root", folder, "--path-map", "a=b", "--gate", "x", "1"]
            code, out = run_main(argv)
        self.assertEqual(code, EXIT_FAILED)
        self.assertIn("::error::cannot read", out)

    def test_lists_test_binaries_from_ctest(self) -> None:
        ctest = {"tests": [{"command": ["/b/two_test"]}, {"command": ["/b/one_test", "--x"]}, {"command": []}]}
        code, out = run_main(["binaries"], json.dumps(ctest))
        self.assertEqual((code, out), (EXIT_CLEAN, "/b/one_test\n/b/two_test\n"))

    def test_fails_when_ctest_lists_nothing(self) -> None:
        for stdin in ("{}", "[]", "not json"):
            with self.subTest(stdin=stdin):
                code, out = run_main(["binaries"], stdin)
                self.assertEqual(code, EXIT_FAILED)
                self.assertIn("::error::", out)


if __name__ == "__main__":
    unittest.main()
