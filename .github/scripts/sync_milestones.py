"""Create the ICS milestones and assign each ICS-### issue to its milestone.

Reads planning/ics-tasks.json, creates any milestone that is missing (matched
by title), then sets the milestone on every issue whose title starts with an
ICS-### identifier listed in the manifest. Safe to re-run: issues that already
carry the right milestone are left alone, and issues without an ICS-### title
are never touched.

Environment: GITHUB_TOKEN (issues: write) and GITHUB_REPOSITORY (owner/name).
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import time
from dataclasses import dataclass

from gh_api import (
    ApiConfig,
    JsonObject,
    api_request,
    fetch_all,
    read_config,
    require_int,
    require_objects,
    require_str,
)
from gh_api import ScriptError as SyncError

WRITE_PAUSE_SECONDS: float = 1.0
ICS_TITLE: re.Pattern[str] = re.compile(r"^(ICS-\d{3})\b")


@dataclass(frozen=True)
class MilestoneSpec:
    title: str
    description: str


@dataclass(frozen=True)
class Manifest:
    milestones: tuple[MilestoneSpec, ...]
    task_milestones: dict[str, str]


@dataclass(frozen=True)
class IssueRef:
    number: int
    ics_id: str
    milestone_title: str | None


@dataclass(frozen=True)
class Assignment:
    issue_number: int
    ics_id: str
    milestone_title: str


def parse_manifest(data: JsonObject) -> Manifest:
    specs: list[MilestoneSpec] = []
    for index, entry in enumerate(require_objects(data, "milestones", "manifest")):
        where: str = f"milestones[{index}]"
        specs.append(MilestoneSpec(require_str(entry, "title", where), require_str(entry, "description", where)))
    known: set[str] = {spec.title for spec in specs}
    task_milestones: dict[str, str] = {}
    for index, entry in enumerate(require_objects(data, "tasks", "manifest")):
        where = f"tasks[{index}]"
        task_id: str = require_str(entry, "id", where)
        milestone: str = require_str(entry, "milestone", where)
        if milestone not in known:
            raise SyncError(f"{where}: unknown milestone '{milestone}'")
        if task_id in task_milestones:
            raise SyncError(f"{where}: duplicate task id '{task_id}'")
        task_milestones[task_id] = milestone
    return Manifest(tuple(specs), task_milestones)


def load_manifest(path: str) -> Manifest:
    try:
        with open(path, encoding="utf-8") as handle:
            data: object = json.load(handle)
    except (OSError, json.JSONDecodeError) as error:
        raise SyncError(f"cannot read manifest {path}: {error}") from error
    if not isinstance(data, dict):
        raise SyncError("manifest must be a JSON object")
    return parse_manifest(data)


def ics_id_from_title(title: str) -> str | None:
    match: re.Match[str] | None = ICS_TITLE.match(title)
    return match.group(1) if match is not None else None


def plan_assignments(issues: list[IssueRef], task_milestones: dict[str, str]) -> list[Assignment]:
    assignments: list[Assignment] = []
    for issue in issues:
        wanted: str | None = task_milestones.get(issue.ics_id)
        if wanted is None or wanted == issue.milestone_title:
            continue
        assignments.append(Assignment(issue.number, issue.ics_id, wanted))
    return assignments


def missing_milestones(specs: tuple[MilestoneSpec, ...], existing: dict[str, int]) -> list[MilestoneSpec]:
    return [spec for spec in specs if spec.title not in existing]


def existing_milestones(config: ApiConfig) -> dict[str, int]:
    milestones: dict[str, int] = {}
    for entry in fetch_all(config, f"/repos/{config.repo}/milestones?state=all"):
        milestones[require_str(entry, "title", "milestone")] = require_int(entry, "number", "milestone")
    return milestones


def create_milestone(config: ApiConfig, spec: MilestoneSpec) -> int:
    payload: JsonObject = {"title": spec.title, "description": spec.description, "state": "open"}
    result: object = api_request(config, "POST", f"/repos/{config.repo}/milestones", payload)
    if not isinstance(result, dict):
        raise SyncError(f"creating milestone '{spec.title}': unexpected response")
    return require_int(result, "number", "created milestone")


def milestone_title_of(entry: JsonObject) -> str | None:
    milestone: object = entry.get("milestone")
    if not isinstance(milestone, dict):
        return None
    title: object = milestone.get("title")
    return title if isinstance(title, str) else None


def to_issue_ref(entry: JsonObject) -> IssueRef | None:
    if "pull_request" in entry:
        return None
    ics_id: str | None = ics_id_from_title(require_str(entry, "title", "issue"))
    if ics_id is None:
        return None
    return IssueRef(require_int(entry, "number", "issue"), ics_id, milestone_title_of(entry))


def list_ics_issues(config: ApiConfig) -> list[IssueRef]:
    refs: list[IssueRef] = []
    for entry in fetch_all(config, f"/repos/{config.repo}/issues?state=all"):
        ref: IssueRef | None = to_issue_ref(entry)
        if ref is not None:
            refs.append(ref)
    return refs


def ensure_milestones(config: ApiConfig, specs: tuple[MilestoneSpec, ...], dry_run: bool) -> dict[str, int]:
    numbers: dict[str, int] = existing_milestones(config)
    for spec in missing_milestones(specs, numbers):
        if dry_run:
            print(f"would create milestone '{spec.title}'")
            continue
        numbers[spec.title] = create_milestone(config, spec)
        print(f"created milestone '{spec.title}' as #{numbers[spec.title]}")
        time.sleep(WRITE_PAUSE_SECONDS)
    return numbers


def apply_assignments(config: ApiConfig, assignments: list[Assignment], numbers: dict[str, int], dry_run: bool) -> None:
    for item in assignments:
        if dry_run:
            print(f"would set #{item.issue_number} {item.ics_id} -> '{item.milestone_title}'")
            continue
        payload: JsonObject = {"milestone": numbers[item.milestone_title]}
        api_request(config, "PATCH", f"/repos/{config.repo}/issues/{item.issue_number}", payload)
        print(f"set #{item.issue_number} {item.ics_id} -> '{item.milestone_title}'")
        time.sleep(WRITE_PAUSE_SECONDS)


def report_unmatched(manifest: Manifest, issues: list[IssueRef]) -> None:
    found: set[str] = {issue.ics_id for issue in issues}
    missing: list[str] = sorted(set(manifest.task_milestones) - found)
    if missing:
        print(f"::warning::no issue found for {len(missing)} task(s): {', '.join(missing)}")


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0] if __doc__ else None)
    parser.add_argument("--manifest", default="planning/ics-tasks.json")
    parser.add_argument("--dry-run", action="store_true", help="print planned changes without applying them")
    return parser.parse_args(argv)


def run(argv: list[str]) -> None:
    args: argparse.Namespace = parse_args(argv)
    manifest: Manifest = load_manifest(str(args.manifest))
    config: ApiConfig = read_config()
    dry_run: bool = bool(args.dry_run)
    numbers: dict[str, int] = ensure_milestones(config, manifest.milestones, dry_run)
    issues: list[IssueRef] = list_ics_issues(config)
    report_unmatched(manifest, issues)
    assignments: list[Assignment] = plan_assignments(issues, manifest.task_milestones)
    apply_assignments(config, assignments, numbers, dry_run)
    print(f"{len(issues)} ICS issues found; {len(assignments)} milestone change(s) {'planned' if dry_run else 'applied'}")


def main(argv: list[str]) -> int:
    try:
        run(argv)
    except SyncError as error:
        print(f"::error::{error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
