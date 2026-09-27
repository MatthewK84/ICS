"""Fail a CodeQL job on its SARIF results (ICS-008, .github/workflows/codeql.yml).

  sarif_check.py FOLDER                     fails if FOLDER's SARIF files hold any result
  sarif_check.py FOLDER --expect RULE ...   fails unless every RULE has a result
  sarif_check.py FOLDER --drop PATH ...     first removes, in place, the results in
                                            files under each PATH prefix

Results that SARIF marks as suppressed still count, because ICS allows no
suppressions. --drop is for generated code, which is not ICS-authored: CodeQL's
paths-ignore does not filter C++ results, so the C++ job drops the results in
cpp/proto/gen/ (ICS-011) before it uploads and checks them. Each finding is
printed as a GitHub error annotation.
"""

from __future__ import annotations

import argparse
import json
import sys
from collections.abc import Sequence
from dataclasses import dataclass
from pathlib import Path

JsonObject = dict[str, object]

ANNOTATION_ESCAPES: tuple[tuple[str, str], ...] = (("%", "%25"), ("\r", "%0D"), ("\n", "%0A"))
PROPERTY_ESCAPES: tuple[tuple[str, str], ...] = (*ANNOTATION_ESCAPES, (":", "%3A"), (",", "%2C"))


class SarifError(Exception):
    """A SARIF file that cannot be read."""


@dataclass(frozen=True, order=True)
class Finding:
    rule: str
    path: str
    line: int
    message: str

    def annotation(self) -> str:
        location = f"file={escape(self.path, PROPERTY_ESCAPES)},line={self.line}"
        return f"::error {location}::{escape(f'{self.rule}: {self.message}', ANNOTATION_ESCAPES)}"


def escape(text: str, table: tuple[tuple[str, str], ...]) -> str:
    for plain, escaped in table:
        text = text.replace(plain, escaped)
    return text


def objects(value: object) -> list[JsonObject]:
    """The JSON objects in a list; anything else counts as empty."""
    if not isinstance(value, list):
        return []
    return [item for item in value if isinstance(item, dict)]


def text_at(data: JsonObject, *keys: str) -> str:
    """A string found by following keys through nested objects, or ""."""
    current: object = data
    for key in keys:
        if not isinstance(current, dict):
            return ""
        current = current.get(key)
    return current if isinstance(current, str) else ""


def rule_of(result: JsonObject) -> str:
    return text_at(result, "ruleId") or text_at(result, "rule", "id") or "unknown-rule"


def first_location(result: JsonObject) -> JsonObject:
    locations = objects(result.get("locations"))
    return locations[0] if locations else {}


def result_path(result: JsonObject) -> str:
    return text_at(first_location(result), "physicalLocation", "artifactLocation", "uri")


def to_finding(result: JsonObject) -> Finding:
    physical = first_location(result).get("physicalLocation")
    region = physical.get("region") if isinstance(physical, dict) else None
    start = region.get("startLine") if isinstance(region, dict) else None
    return Finding(
        rule_of(result),
        result_path(result) or "unknown-file",
        start if isinstance(start, int) else 1,
        text_at(result, "message", "text"),
    )


def findings_in(document: object) -> list[Finding]:
    if not isinstance(document, dict):
        raise SarifError("a SARIF file must hold a JSON object")
    found: list[Finding] = []
    for run in objects(document.get("runs")):
        found.extend(to_finding(result) for result in objects(run.get("results")))
    return found


def sarif_files(folder: Path) -> list[Path]:
    files = sorted(folder.glob("*.sarif"))
    if not files:
        raise SarifError(f"no SARIF files in {folder}")
    return files


def read_document(path: Path) -> object:
    try:
        document: object = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise SarifError(f"cannot read {path}: {error}") from error
    return document


def load_findings(folder: Path) -> list[Finding]:
    found: list[Finding] = []
    for path in sarif_files(folder):
        found.extend(findings_in(read_document(path)))
    return sorted(found)


def drop_from(document: object, prefixes: Sequence[str]) -> int:
    """Remove the results in files under any prefix from a SARIF document; return how many."""
    if not isinstance(document, dict):
        raise SarifError("a SARIF file must hold a JSON object")
    dropped = 0
    for run in objects(document.get("runs")):
        results = run.get("results")
        if not isinstance(results, list):
            continue
        kept = [r for r in results if not (isinstance(r, dict) and result_path(r).startswith(tuple(prefixes)))]
        dropped += len(results) - len(kept)
        run["results"] = kept
    return dropped


def drop_results(folder: Path, prefixes: Sequence[str]) -> int:
    """Rewrite each SARIF file in folder without the results under the prefixes."""
    dropped = 0
    for path in sarif_files(folder):
        document = read_document(path)
        dropped += drop_from(document, prefixes)
        try:
            path.write_text(json.dumps(document), encoding="utf-8")
        except OSError as error:
            raise SarifError(f"cannot write {path}: {error}") from error
    return dropped


def missing_rules(found: Sequence[Finding], expected: Sequence[str]) -> list[str]:
    present = {finding.rule for finding in found}
    return [rule for rule in expected if rule not in present]


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(prog="sarif_check", description="Fail a CodeQL job on its SARIF results.")
    parser.add_argument("folder", type=Path, help="folder holding the .sarif files")
    parser.add_argument("--expect", nargs="+", default=[], metavar="RULE", help="rules that must each have a result")
    parser.add_argument(
        "--drop", nargs="+", default=[], metavar="PATH", help="first remove the results under these path prefixes"
    )
    return parser.parse_args(argv)


def verdict(found: Sequence[Finding], expected: Sequence[str]) -> int:
    if expected:
        missing = missing_rules(found, expected)
        for rule in missing:
            sys.stdout.write(f"::error::seeded defect not found: no result for {rule}\n")
        sys.stdout.write(f"sarif_check: {len(expected) - len(missing)} of {len(expected)} expected rules found\n")
        return 1 if missing else 0
    for finding in found:
        sys.stdout.write(finding.annotation() + "\n")
    sys.stdout.write(f"sarif_check: {len(found)} finding(s)\n")
    return 1 if found else 0


def main(argv: Sequence[str]) -> int:
    args = parse_args(argv)
    try:
        if args.drop:
            dropped = drop_results(args.folder, args.drop)
            sys.stdout.write(f"sarif_check: dropped {dropped} result(s) under {', '.join(args.drop)}\n")
        found = load_findings(args.folder)
    except SarifError as error:
        sys.stdout.write(f"::error::{error}\n")
        return 1
    return verdict(found, args.expect)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
