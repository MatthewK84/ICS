"""Unit tests for sync_milestones.py (stdlib unittest, no network)."""

from __future__ import annotations

import unittest
from pathlib import Path

import gh_api as gh
import sync_milestones as sm

MANIFEST_PATH: Path = Path(__file__).resolve().parents[2] / "planning" / "ics-tasks.json"


def small_manifest() -> gh.JsonObject:
    return {
        "milestones": [
            {"title": "M0 Foundation", "description": "first"},
            {"title": "M1 Core services", "description": "second"},
        ],
        "tasks": [
            {"id": "ICS-001", "milestone": "M0 Foundation"},
            {"id": "ICS-015", "milestone": "M1 Core services"},
        ],
    }


class ManifestTests(unittest.TestCase):
    def test_parses_milestones_and_tasks(self) -> None:
        manifest: sm.Manifest = sm.parse_manifest(small_manifest())
        self.assertEqual([spec.title for spec in manifest.milestones], ["M0 Foundation", "M1 Core services"])
        self.assertEqual(manifest.task_milestones["ICS-015"], "M1 Core services")

    def test_rejects_unknown_milestone(self) -> None:
        data: gh.JsonObject = small_manifest()
        data["tasks"] = [{"id": "ICS-001", "milestone": "M9 Nowhere"}]
        with self.assertRaises(gh.ScriptError):
            sm.parse_manifest(data)

    def test_rejects_duplicate_task(self) -> None:
        data: gh.JsonObject = small_manifest()
        data["tasks"] = [{"id": "ICS-001", "milestone": "M0 Foundation"}] * 2
        with self.assertRaises(gh.ScriptError):
            sm.parse_manifest(data)

    def test_repository_manifest_covers_every_planned_task(self) -> None:
        manifest: sm.Manifest = sm.load_manifest(str(MANIFEST_PATH))
        # ICS-080 was dropped (ICS-013).
        expected: set[str] = {f"ICS-{number:03d}" for number in range(1, 98)} - {"ICS-080"}
        self.assertEqual(len(manifest.milestones), 7)
        self.assertEqual(set(manifest.task_milestones), expected)


class PlanningTests(unittest.TestCase):
    def test_reads_ics_id_only_at_title_start(self) -> None:
        self.assertEqual(sm.ics_id_from_title("ICS-042 Fit camera intrinsics"), "ICS-042")
        self.assertIsNone(sm.ics_id_from_title("Follow-up for ICS-042"))
        self.assertIsNone(sm.ics_id_from_title("ICS-42 short id"))

    def test_plans_only_missing_or_wrong_milestones(self) -> None:
        issues: list[sm.IssueRef] = [
            sm.IssueRef(1, "ICS-001", None),
            sm.IssueRef(15, "ICS-015", "M1 Core services"),
            sm.IssueRef(16, "ICS-016", "M0 Foundation"),
            sm.IssueRef(200, "ICS-200", None),
        ]
        tasks: dict[str, str] = {"ICS-001": "M0 Foundation", "ICS-015": "M1 Core services", "ICS-016": "M1 Core services"}
        planned: list[sm.Assignment] = sm.plan_assignments(issues, tasks)
        self.assertEqual([(a.issue_number, a.milestone_title) for a in planned], [(1, "M0 Foundation"), (16, "M1 Core services")])

    def test_lists_only_missing_milestones(self) -> None:
        specs: tuple[sm.MilestoneSpec, ...] = sm.parse_manifest(small_manifest()).milestones
        missing: list[sm.MilestoneSpec] = sm.missing_milestones(specs, {"M0 Foundation": 1})
        self.assertEqual([spec.title for spec in missing], ["M1 Core services"])

    def test_skips_pull_requests_and_untitled_issues(self) -> None:
        self.assertIsNone(sm.to_issue_ref({"number": 3, "title": "ICS-003 x", "pull_request": {}}))
        self.assertIsNone(sm.to_issue_ref({"number": 4, "title": "Unrelated"}))
        ref: sm.IssueRef | None = sm.to_issue_ref({"number": 5, "title": "ICS-005 x", "milestone": {"title": "M0 Foundation"}})
        self.assertEqual(ref, sm.IssueRef(5, "ICS-005", "M0 Foundation"))


if __name__ == "__main__":
    unittest.main()
