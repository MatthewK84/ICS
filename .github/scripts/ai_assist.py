"""Check a pull request's AI-assistance declaration and its human approvals.

Two entry points, one per required status check (ICS-003, docs/ai-usage.md):

  ai_assist.py declaration  Fails unless exactly one of "No AI assistance" and
                            "AI-assisted" is ticked and, when AI-assisted, the
                            Tool, Model and version, and Scope fields are
                            filled. Keeps the ai-assisted label in step and
                            writes ai_assisted=true|false to GITHUB_OUTPUT.
  ai_assist.py review       Fails until an AI-assisted pull request has enough
                            approvals from humans other than its author.

The pull-request text is untrusted: it is read from the event file
(GITHUB_EVENT_PATH) and never passed through a shell.

Environment: GITHUB_EVENT_PATH, GITHUB_TOKEN, GITHUB_REPOSITORY and, for the
declaration check, GITHUB_OUTPUT.
"""

from __future__ import annotations

import enum
import json
import os
import re
import sys
import urllib.parse
from collections.abc import Mapping
from dataclasses import dataclass

import gh_api as gh

SECTION_HEADING: str = "## AI assistance"
NO_AI_BOX: str = "no ai assistance"
AI_BOX: str = "ai-assisted"
FIELD_NAMES: tuple[str, ...] = ("Tool", "Model and version", "Scope")
AI_LABEL: str = "ai-assisted"
REQUIRED_HUMAN_APPROVALS: int = 2
DECISIVE_REVIEW_STATES: frozenset[str] = frozenset({"APPROVED", "CHANGES_REQUESTED", "DISMISSED"})
COMMENT: re.Pattern[str] = re.compile(r"<!--.*?-->", re.DOTALL)
BOX_LINE: re.Pattern[str] = re.compile(r"^\s*[-*]\s+\[(?P<mark>[ xX])\]\s+(?P<text>.+?)\s*$")
FIELD_LINE: re.Pattern[str] = re.compile(r"^\s*[-*]\s+\*\*(?P<name>[^*]+?):\*\*(?P<value>.*)$")


class LabelAction(enum.Enum):
    ADD = "add"
    REMOVE = "remove"
    NONE = "none"


@dataclass(frozen=True)
class Declaration:
    section_found: bool
    no_ai: bool
    ai_assisted: bool
    fields: Mapping[str, str]


@dataclass(frozen=True)
class Review:
    login: str
    is_bot: bool
    state: str


@dataclass(frozen=True)
class PullRequest:
    number: int
    author: str
    body: str
    labels: frozenset[str]
    from_fork: bool


def ai_section(body: str) -> str | None:
    lines: list[str] = COMMENT.sub("", body).splitlines()
    start: int | None = None
    for index, line in enumerate(lines):
        if line.strip().lower() == SECTION_HEADING.lower():
            start = index + 1
            break
    if start is None:
        return None
    end: int = len(lines)
    for index in range(start, len(lines)):
        if lines[index].startswith("## "):
            end = index
            break
    return "\n".join(lines[start:end])


def parse_declaration(body: str) -> Declaration:
    section: str | None = ai_section(body)
    if section is None:
        return Declaration(False, False, False, {})
    ticked: set[str] = set()
    fields: dict[str, str] = {}
    for line in section.splitlines():
        box: re.Match[str] | None = BOX_LINE.match(line)
        if box is not None and box.group("mark") in "xX":
            ticked.add(box.group("text").strip().lower())
        field: re.Match[str] | None = FIELD_LINE.match(line)
        if field is not None:
            fields[field.group("name").strip()] = field.group("value").strip()
    return Declaration(True, NO_AI_BOX in ticked, AI_BOX in ticked, fields)


def declaration_errors(decl: Declaration) -> list[str]:
    if not decl.section_found:
        return [f"The description has no '{SECTION_HEADING}' section. Start from the pull-request template."]
    if decl.no_ai and decl.ai_assisted:
        return ["Tick only one box under AI assistance: 'No AI assistance' or 'AI-assisted'."]
    if not decl.no_ai and not decl.ai_assisted:
        return ["Answer the AI assistance question: tick 'No AI assistance' or 'AI-assisted'."]
    if decl.no_ai:
        return []
    return [f"AI-assisted: fill in '{name}'." for name in FIELD_NAMES if not decl.fields.get(name)]


def is_answered_ai(decl: Declaration) -> bool:
    return decl.ai_assisted and not decl.no_ai


def label_action(decl: Declaration, labels: frozenset[str]) -> LabelAction:
    if is_answered_ai(decl) and AI_LABEL not in labels:
        return LabelAction.ADD
    if decl.no_ai and not decl.ai_assisted and AI_LABEL in labels:
        return LabelAction.REMOVE
    return LabelAction.NONE


def is_bot_account(login: str, account_type: object) -> bool:
    return account_type == "Bot" or login.endswith("[bot]")


def to_review(entry: gh.JsonObject) -> Review | None:
    user: object = entry.get("user")
    state: object = entry.get("state")
    if not isinstance(user, dict) or not isinstance(state, str):
        return None
    login: object = user.get("login")
    if not isinstance(login, str) or not login:
        return None
    return Review(login, is_bot_account(login, user.get("type")), state)


def human_approvals(reviews: list[Review], author: str) -> int:
    latest: dict[str, str] = {}
    for review in reviews:
        if review.is_bot or review.state not in DECISIVE_REVIEW_STATES:
            continue
        latest[review.login.lower()] = review.state
    approvers: set[str] = {login for login, state in latest.items() if state == "APPROVED"}
    return len(approvers - {author.lower()})


def label_names(pull: gh.JsonObject) -> frozenset[str]:
    names: set[str] = set()
    for label in gh.require_objects(pull, "labels", "pull_request"):
        name: object = label.get("name")
        if isinstance(name, str):
            names.add(name)
    return frozenset(names)


def repo_name(pull: gh.JsonObject, side: str) -> str:
    ref: gh.JsonObject = gh.require_object(pull, side, "pull_request")
    repo: object = ref.get("repo")
    if not isinstance(repo, dict):
        return ""
    name: object = repo.get("full_name")
    return name if isinstance(name, str) else ""


def to_pull_request(event: gh.JsonObject) -> PullRequest:
    pull: gh.JsonObject = gh.require_object(event, "pull_request", "event")
    body: object = pull.get("body")
    user: gh.JsonObject = gh.require_object(pull, "user", "pull_request")
    return PullRequest(
        gh.require_int(pull, "number", "pull_request"),
        gh.require_str(user, "login", "pull_request.user"),
        body if isinstance(body, str) else "",
        label_names(pull),
        repo_name(pull, "head") != repo_name(pull, "base"),
    )


def load_event(path: str) -> PullRequest:
    try:
        with open(path, encoding="utf-8") as handle:
            event: object = json.load(handle)
    except (OSError, json.JSONDecodeError) as error:
        raise gh.ScriptError(f"cannot read event file {path}: {error}") from error
    if not isinstance(event, dict):
        raise gh.ScriptError("event file must hold a JSON object")
    return to_pull_request(event)


def sync_label(config: gh.ApiConfig, pull: PullRequest, action: LabelAction) -> None:
    if action is LabelAction.NONE:
        return
    if pull.from_fork:
        print(f"::warning::Pull request is from a fork; cannot {action.value} the '{AI_LABEL}' label.")
        return
    base: str = f"/repos/{config.repo}/issues/{pull.number}/labels"
    try:
        if action is LabelAction.ADD:
            gh.api_request(config, "POST", base, {"labels": [AI_LABEL]})
        else:
            gh.api_request(config, "DELETE", f"{base}/{urllib.parse.quote(AI_LABEL)}")
    except gh.ScriptError as error:
        print(f"::warning::Could not {action.value} the '{AI_LABEL}' label: {error}")
        return
    print(f"Label '{AI_LABEL}': {action.value}")


def write_output(name: str, value: str) -> None:
    path: str = os.environ.get("GITHUB_OUTPUT", "")
    if not path:
        return
    with open(path, "a", encoding="utf-8") as handle:
        handle.write(f"{name}={value}\n")


def run_declaration(config: gh.ApiConfig, pull: PullRequest) -> list[str]:
    decl: Declaration = parse_declaration(pull.body)
    write_output("ai_assisted", "true" if is_answered_ai(decl) else "false")
    sync_label(config, pull, label_action(decl, pull.labels))
    errors: list[str] = declaration_errors(decl)
    if not errors:
        print("AI assistance declared: " + ("AI-assisted" if is_answered_ai(decl) else "no AI assistance"))
    return errors


def run_review(config: gh.ApiConfig, pull: PullRequest) -> list[str]:
    entries: list[gh.JsonObject] = gh.fetch_all(config, f"/repos/{config.repo}/pulls/{pull.number}/reviews")
    reviews: list[Review] = [review for review in map(to_review, entries) if review is not None]
    count: int = human_approvals(reviews, pull.author)
    print(f"Human approvals other than the author: {count} of {REQUIRED_HUMAN_APPROVALS} required")
    if count >= REQUIRED_HUMAN_APPROVALS:
        return []
    return [
        f"AI-assisted change: {count} of {REQUIRED_HUMAN_APPROVALS} required approvals from people other than "
        "the author. Bot approvals do not count."
    ]


def run(argv: list[str]) -> list[str]:
    if len(argv) != 1 or argv[0] not in ("declaration", "review"):
        raise gh.ScriptError("usage: ai_assist.py declaration|review")
    pull: PullRequest = load_event(os.environ.get("GITHUB_EVENT_PATH", ""))
    config: gh.ApiConfig = gh.read_config()
    if argv[0] == "declaration":
        return run_declaration(config, pull)
    return run_review(config, pull)


def main(argv: list[str]) -> int:
    try:
        errors: list[str] = run(argv)
    except gh.ScriptError as error:
        print(f"::error::{error}", file=sys.stderr)
        return 1
    for message in errors:
        print(f"::error::{message}", file=sys.stderr)
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
