"""Fail a CodeQL job on its SARIF results (ICS-008, .github/workflows/codeql.yml).

  sarif_check.py FOLDER                     fails if FOLDER's SARIF files hold any result
  sarif_check.py FOLDER --expect RULE ...   fails unless every RULE has a result

Results that SARIF marks as suppressed still count, because ICS allows no
suppressions. Each finding is printed as a GitHub error annotation.
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


def to_finding(result: JsonObject) -> Finding:
    locations = objects(result.get("locations"))
    location: JsonObject = locations[0] if locations else {}
    physical = location.get("physicalLocation")
    region = physical.get("region") if isinstance(physical, dict) else None
    start = region.get("startLine") if isinstance(region, dict) else None
    return Finding(
        rule_of(result),
        text_at(location, "physicalLocation", "artifactLocation", "uri") or "unknown-file",
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


def load_findings(folder: Path) -> list[Finding]:
    files = sorted(folder.glob("*.sarif"))
    if not files:
        raise SarifError(f"no SARIF files in {folder}")
    found: list[Finding] = []
    for path in files:
        try:
            document: object = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as error:
            raise SarifError(f"cannot read {path}: {error}") from error
        found.extend(findings_in(document))
    return sorted(found)


def missing_rules(found: Sequence[Finding], expected: Sequence[str]) -> list[str]:
    present = {finding.rule for finding in found}
    return [rule for rule in expected if rule not in present]


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(prog="sarif_check", description="Fail a CodeQL job on its SARIF results.")
    parser.add_argument("folder", type=Path, help="folder holding the .sarif files")
    parser.add_argument("--expect", nargs="+", default=[], metavar="RULE", help="rules that must each have a result")
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
        found = load_findings(args.folder)
    except SarifError as error:
        sys.stdout.write(f"::error::{error}\n")
        return 1
    return verdict(found, args.expect)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
