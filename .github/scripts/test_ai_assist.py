"""Unit tests for ai_assist.py (stdlib unittest, no network)."""

from __future__ import annotations

import json
import os
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import ai_assist as ai
import gh_api as gh

TEMPLATE_PATH: Path = Path(__file__).resolve().parents[1] / "pull_request_template.md"
CONFIG: gh.ApiConfig = gh.ApiConfig("token", "owner/repo")


def body(no_ai: str = " ", ai_box: str = " ", tool: str = "", model: str = "", scope: str = "") -> str:
    return (
        "## Summary\n\n- Something.\n\n## AI assistance\n\n"
        f"- [{no_ai}] No AI assistance\n- [{ai_box}] AI-assisted\n\n"
        f"- **Tool:** {tool}\n- **Model and version:** {model}\n- **Scope:** {scope}\n\n"
        "## Definition of Done\n\n- [x] AI-assisted changes labeled and reviewed by a human (DoWI 8430.01 §3.6).\n"
    )


def pull(text: str, labels: frozenset[str] = frozenset(), from_fork: bool = False) -> ai.PullRequest:
    return ai.PullRequest(7, text, labels, from_fork)


def event(text: object, head: str = "owner/repo") -> gh.JsonObject:
    return {
        "pull_request": {
            "number": 7,
            "body": text,
            "labels": [{"name": "path-b"}],
            "head": {"repo": {"full_name": head}},
            "base": {"repo": {"full_name": "owner/repo"}},
        }
    }


class DeclarationTests(unittest.TestCase):
    def test_unanswered_template_fails(self) -> None:
        decl: ai.Declaration = ai.parse_declaration(TEMPLATE_PATH.read_text(encoding="utf-8"))
        self.assertTrue(decl.section_found)
        self.assertEqual(
            ai.declaration_errors(decl), ["Answer the AI assistance question: tick 'No AI assistance' or 'AI-assisted'."]
        )

    def test_template_fields_start_empty(self) -> None:
        text: str = TEMPLATE_PATH.read_text(encoding="utf-8").replace("- [ ] AI-assisted", "- [x] AI-assisted")
        errors: list[str] = ai.declaration_errors(ai.parse_declaration(text))
        self.assertEqual(len(errors), len(ai.REQUIRED_FIELDS))

    def test_no_ai_passes_with_empty_fields(self) -> None:
        self.assertEqual(ai.declaration_errors(ai.parse_declaration(body(no_ai="x"))), [])

    def test_uppercase_mark_counts(self) -> None:
        decl: ai.Declaration = ai.parse_declaration(body(ai_box="X", tool="T", model="M 1", scope="S"))
        self.assertTrue(decl.ai_assisted)
        self.assertEqual(ai.declaration_errors(decl), [])

    def test_both_boxes_fail(self) -> None:
        errors: list[str] = ai.declaration_errors(ai.parse_declaration(body(no_ai="x", ai_box="x")))
        self.assertEqual(errors, ["Tick only one box under AI assistance: 'No AI assistance' or 'AI-assisted'."])

    def test_ai_assisted_needs_tool_and_scope(self) -> None:
        errors: list[str] = ai.declaration_errors(ai.parse_declaration(body(ai_box="x", model="M 1")))
        self.assertEqual(errors, ["AI-assisted: fill in 'Tool'.", "AI-assisted: fill in 'Scope'."])

    def test_model_and_version_is_optional(self) -> None:
        errors: list[str] = ai.declaration_errors(ai.parse_declaration(body(ai_box="x", tool="T", scope="S")))
        self.assertEqual(errors, [])

    def test_comments_do_not_fill_fields(self) -> None:
        text: str = body(ai_box="x", tool="<!-- hint -->", model="M", scope="S")
        self.assertEqual(ai.declaration_errors(ai.parse_declaration(text)), ["AI-assisted: fill in 'Tool'."])

    def test_boxes_outside_the_section_are_ignored(self) -> None:
        text: str = body().replace("## AI assistance", "## AI assistance\n\n## Other\n\n- [x] AI-assisted")
        decl: ai.Declaration = ai.parse_declaration(text)
        self.assertFalse(decl.ai_assisted)

    def test_missing_section_fails(self) -> None:
        errors: list[str] = ai.declaration_errors(ai.parse_declaration("Just a description."))
        self.assertEqual(len(errors), 1)
        self.assertIn("## AI assistance", errors[0])


class LabelTests(unittest.TestCase):
    def test_label_follows_the_answer(self) -> None:
        ai_decl: ai.Declaration = ai.parse_declaration(body(ai_box="x"))
        no_decl: ai.Declaration = ai.parse_declaration(body(no_ai="x"))
        unanswered: ai.Declaration = ai.parse_declaration(body())
        labelled: frozenset[str] = frozenset({ai.AI_LABEL})
        self.assertIs(ai.label_action(ai_decl, frozenset()), ai.LabelAction.ADD)
        self.assertIs(ai.label_action(ai_decl, labelled), ai.LabelAction.NONE)
        self.assertIs(ai.label_action(no_decl, labelled), ai.LabelAction.REMOVE)
        self.assertIs(ai.label_action(no_decl, frozenset()), ai.LabelAction.NONE)
        self.assertIs(ai.label_action(unanswered, labelled), ai.LabelAction.NONE)

    def test_sync_label_skips_forks_and_none(self) -> None:
        with mock.patch.object(gh, "api_request") as request:
            ai.sync_label(CONFIG, pull("", from_fork=True), ai.LabelAction.ADD)
            ai.sync_label(CONFIG, pull(""), ai.LabelAction.NONE)
        request.assert_not_called()

    def test_sync_label_adds_and_removes(self) -> None:
        with mock.patch.object(gh, "api_request") as request:
            ai.sync_label(CONFIG, pull(""), ai.LabelAction.ADD)
            ai.sync_label(CONFIG, pull(""), ai.LabelAction.REMOVE)
        self.assertEqual(request.call_args_list[0].args[1:3], ("POST", "/repos/owner/repo/issues/7/labels"))
        self.assertEqual(request.call_args_list[1].args[1:3], ("DELETE", "/repos/owner/repo/issues/7/labels/ai-assisted"))

    def test_sync_label_failure_is_a_warning(self) -> None:
        with mock.patch.object(gh, "api_request", side_effect=gh.ScriptError("403")):
            ai.sync_label(CONFIG, pull(""), ai.LabelAction.ADD)


class EventTests(unittest.TestCase):
    def test_reads_pull_request_from_event(self) -> None:
        pr: ai.PullRequest = ai.to_pull_request(event("text"))
        self.assertEqual(pr, ai.PullRequest(7, "text", frozenset({"path-b"}), False))
        self.assertTrue(ai.to_pull_request(event(None, head="fork/repo")).from_fork)
        self.assertEqual(ai.to_pull_request(event(None)).body, "")

    def test_rejects_malformed_events(self) -> None:
        with self.assertRaises(gh.ScriptError):
            ai.to_pull_request({"pull_request": "nope"})
        with tempfile.TemporaryDirectory() as folder:
            path: Path = Path(folder) / "event.json"
            path.write_text("[1]", encoding="utf-8")
            with self.assertRaises(gh.ScriptError):
                ai.load_event(str(path))
            with self.assertRaises(gh.ScriptError):
                ai.load_event(str(Path(folder) / "missing.json"))


class MainTests(unittest.TestCase):
    def run_main(self, text: str) -> int:
        with tempfile.TemporaryDirectory() as folder:
            event_path: Path = Path(folder) / "event.json"
            event_path.write_text(json.dumps(event(text)), encoding="utf-8")
            env: dict[str, str] = {
                "GITHUB_EVENT_PATH": str(event_path),
                "GITHUB_TOKEN": "token",
                "GITHUB_REPOSITORY": "owner/repo",
            }
            with mock.patch.dict(os.environ, env), mock.patch.object(gh, "api_request"):
                return ai.main(["declaration"])

    def test_declaration_passes_when_answered(self) -> None:
        self.assertEqual(self.run_main(body(no_ai="x")), 0)
        self.assertEqual(self.run_main(body(ai_box="x", tool="T", scope="S")), 0)

    def test_declaration_fails_when_unanswered(self) -> None:
        self.assertEqual(self.run_main(body()), 1)
        self.assertEqual(self.run_main(body(ai_box="x", model="M 1")), 1)

    def test_bad_usage_fails(self) -> None:
        self.assertEqual(ai.main(["bogus"]), 1)
        self.assertEqual(ai.main(["review"]), 1)


if __name__ == "__main__":
    unittest.main()
