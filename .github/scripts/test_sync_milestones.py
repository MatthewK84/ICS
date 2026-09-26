"""Unit tests for sync_milestones.py (stdlib unittest, no network)."""

from __future__ import annotations

import email.message
import io
import time
import unittest
import urllib.error
from pathlib import Path
from unittest import mock

import sync_milestones as sm

MANIFEST_PATH: Path = Path(__file__).resolve().parents[2] / "planning" / "ics-tasks.json"


def http_error(code: int, headers: dict[str, str] | None = None) -> urllib.error.HTTPError:
    message = email.message.Message()
    for key, value in (headers or {}).items():
        message[key] = value
    return urllib.error.HTTPError("https://api.github.com/x", code, "error", message, io.BytesIO(b"{}"))


def small_manifest() -> sm.JsonObject:
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
        data: sm.JsonObject = small_manifest()
        data["tasks"] = [{"id": "ICS-001", "milestone": "M9 Nowhere"}]
        with self.assertRaises(sm.SyncError):
            sm.parse_manifest(data)

    def test_rejects_duplicate_task(self) -> None:
        data: sm.JsonObject = small_manifest()
        data["tasks"] = [{"id": "ICS-001", "milestone": "M0 Foundation"}] * 2
        with self.assertRaises(sm.SyncError):
            sm.parse_manifest(data)

    def test_repository_manifest_covers_all_97_tasks(self) -> None:
        manifest: sm.Manifest = sm.load_manifest(str(MANIFEST_PATH))
        expected: set[str] = {f"ICS-{number:03d}" for number in range(1, 98)}
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


class RetryTests(unittest.TestCase):
    def setUp(self) -> None:
        self.config = sm.ApiConfig("token", "owner/repo")
        patcher = mock.patch.object(time, "sleep")
        self.sleep = patcher.start()
        self.addCleanup(patcher.stop)

    def test_classifies_retryable_errors(self) -> None:
        self.assertTrue(sm.is_retryable(http_error(502)))
        self.assertTrue(sm.is_retryable(http_error(403, {"retry-after": "5"})))
        self.assertFalse(sm.is_retryable(http_error(403)))
        self.assertFalse(sm.is_retryable(http_error(422)))

    def test_retries_then_succeeds(self) -> None:
        with mock.patch.object(sm, "send_once", side_effect=[http_error(503), {"ok": True}]):
            self.assertEqual(sm.api_request(self.config, "GET", "/x"), {"ok": True})
        self.assertEqual(self.sleep.call_count, 1)

    def test_gives_up_after_bounded_attempts(self) -> None:
        failures: list[urllib.error.HTTPError] = [http_error(500)] * sm.MAX_ATTEMPTS
        with mock.patch.object(sm, "send_once", side_effect=failures), self.assertRaises(sm.SyncError):
            sm.api_request(self.config, "GET", "/x")
        self.assertEqual(self.sleep.call_count, sm.MAX_ATTEMPTS - 1)

    def test_fails_fast_on_client_error(self) -> None:
        with mock.patch.object(sm, "send_once", side_effect=[http_error(422)]), self.assertRaises(sm.SyncError):
            sm.api_request(self.config, "POST", "/x", {"title": "t"})
        self.sleep.assert_not_called()

    def test_fetch_all_follows_pages(self) -> None:
        full_page: list[sm.JsonObject] = [{"n": index} for index in range(sm.PAGE_SIZE)]
        with mock.patch.object(sm, "api_request", side_effect=[full_page, [{"n": -1}]]) as request:
            items: list[sm.JsonObject] = sm.fetch_all(self.config, "/items?state=all")
        self.assertEqual(len(items), sm.PAGE_SIZE + 1)
        self.assertIn("state=all&per_page=100&page=2", request.call_args_list[1].args[2])


if __name__ == "__main__":
    unittest.main()
