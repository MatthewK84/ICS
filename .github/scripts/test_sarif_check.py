"""Unit tests for sarif_check.py (stdlib unittest, no network)."""

from __future__ import annotations

import io
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import sarif_check as sc

EXIT_CLEAN: int = 0
EXIT_FAILED: int = 1
CPP_RESULT: sc.JsonObject = {
    "ruleId": "cpp/wrong-type-format-argument",
    "message": {"text": "This argument should be of type 'char *'."},
    "locations": [{"physicalLocation": {"artifactLocation": {"uri": "src/a.cpp"}, "region": {"startLine": 7}}}],
}


def sarif(*results: object) -> sc.JsonObject:
    return {"version": "2.1.0", "runs": [{"results": list(results)}]}


def write_sarif(folder: Path, name: str, document: object) -> None:
    (folder / name).write_text(json.dumps(document), encoding="utf-8")


def run_main(argv: list[str]) -> tuple[int, str]:
    with patch("sys.stdout", new_callable=io.StringIO) as out:
        code = sc.main(argv)
    return code, out.getvalue()


class FindingTests(unittest.TestCase):
    def test_reads_rule_location_and_message(self) -> None:
        self.assertEqual(
            sc.findings_in(sarif(CPP_RESULT)),
            [sc.Finding("cpp/wrong-type-format-argument", "src/a.cpp", 7, "This argument should be of type 'char *'.")],
        )

    def test_falls_back_when_fields_are_missing(self) -> None:
        result: sc.JsonObject = {"rule": {"id": "py/x"}, "locations": [{"physicalLocation": "bad"}]}
        self.assertEqual(sc.findings_in(sarif(result, {})), [
            sc.Finding("py/x", "unknown-file", 1, ""),
            sc.Finding("unknown-rule", "unknown-file", 1, ""),
        ])
        self.assertEqual(sc.findings_in({"runs": "not a list"}), [])

    def test_suppressed_results_still_count(self) -> None:
        suppressed: sc.JsonObject = {**CPP_RESULT, "suppressions": [{"kind": "inSource"}]}
        self.assertEqual(len(sc.findings_in(sarif(suppressed))), 1)

    def test_rejects_a_document_that_is_not_an_object(self) -> None:
        with self.assertRaises(sc.SarifError):
            sc.findings_in([1, 2])

    def test_annotation_escapes_special_characters(self) -> None:
        finding = sc.Finding("js/x", "a,b:c.ts", 3, "50% done\nnext")
        self.assertEqual(finding.annotation(), "::error file=a%2Cb%3Ac.ts,line=3::js/x: 50%25 done%0Anext")


class MainTests(unittest.TestCase):
    def test_clean_results_pass(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            write_sarif(Path(folder), "python.sarif", sarif())
            self.assertEqual(run_main([folder]), (EXIT_CLEAN, "sarif_check: 0 finding(s)\n"))

    def test_any_finding_fails(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            write_sarif(Path(folder), "cpp.sarif", sarif(CPP_RESULT))
            code, output = run_main([folder])
        self.assertEqual(code, EXIT_FAILED)
        self.assertIn("::error file=src/a.cpp,line=7::cpp/wrong-type-format-argument", output)

    def test_expected_rules_must_all_fire(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            write_sarif(Path(folder), "cpp.sarif", sarif(CPP_RESULT))
            self.assertEqual(run_main([folder, "--expect", "cpp/wrong-type-format-argument"])[0], EXIT_CLEAN)
            code, output = run_main([folder, "--expect", "cpp/wrong-type-format-argument", "cpp/other"])
        self.assertEqual(code, EXIT_FAILED)
        self.assertIn("no result for cpp/other", output)

    def test_drop_removes_results_under_a_path_before_checking(self) -> None:
        generated: sc.JsonObject = {
            "ruleId": "cpp/commented-out-code",
            "locations": [{"physicalLocation": {"artifactLocation": {"uri": "cpp/proto/gen/ics/a.pb.h"}}}],
        }
        with tempfile.TemporaryDirectory() as folder:
            write_sarif(Path(folder), "cpp.sarif", sarif(generated, generated, CPP_RESULT, "not an object"))
            code, output = run_main([folder, "--drop", "cpp/proto/gen/", "web/gen/"])
            kept = json.loads((Path(folder) / "cpp.sarif").read_text(encoding="utf-8"))
        self.assertEqual(code, EXIT_FAILED)
        self.assertIn("dropped 2 result(s) under cpp/proto/gen/, web/gen/", output)
        self.assertIn("sarif_check: 1 finding(s)", output)
        self.assertEqual(kept["runs"][0]["results"], [CPP_RESULT, "not an object"])

    def test_drop_leaves_runs_without_results_alone(self) -> None:
        document: sc.JsonObject = {"runs": [{"tool": {}}, "not a run"]}
        self.assertEqual(sc.drop_from(document, ("cpp/",)), 0)
        self.assertEqual(document, {"runs": [{"tool": {}}, "not a run"]})
        with self.assertRaises(sc.SarifError):
            sc.drop_from([], ("cpp/",))

    def test_drop_reports_unwritable_files(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            write_sarif(Path(folder), "cpp.sarif", sarif(CPP_RESULT))
            with patch.object(Path, "write_text", side_effect=OSError("read-only")):
                code, output = run_main([folder, "--drop", "cpp/proto/gen/"])
        self.assertEqual(code, EXIT_FAILED)
        self.assertIn("cannot write", output)

    def test_missing_or_broken_files_fail(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            self.assertIn("no SARIF files", run_main([folder])[1])
            (Path(folder) / "bad.sarif").write_text("{", encoding="utf-8")
            self.assertEqual(run_main([folder])[0], EXIT_FAILED)


if __name__ == "__main__":
    unittest.main()
