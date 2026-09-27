"""Build, check and publish the CI evidence (ICS-009, deploy/evidence/README.md).

  evidence.py predicate --tools FILE --out FILE
      Writes a SLSA v1 provenance predicate for this GitHub Actions run, in the
      format GitHub uses for workflow builds. It names the workflow, the commit
      and every tool pinned in FILE, with its sha256.
  evidence.py statement --subject FILE --name NAME --predicate-type URI --predicate FILE --out FILE
      Wraps a predicate in an in-toto v1 statement about the subject's sha256.
  evidence.py check-sbom FILE --require TYPE [TYPE ...]
      Fails unless the CycloneDX SBOM lists a package of each purl TYPE, so a
      scanner that silently stops finding an ecosystem fails the build.
  evidence.py publish BUNDLE [BUNDLE ...]
      Uploads each Sigstore bundle to the repository's attestations.

Environment: predicate reads GitHub Actions' default variables; publish reads
GITHUB_TOKEN and GITHUB_REPOSITORY.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import sys
from collections.abc import Callable, Mapping, Sequence
from dataclasses import dataclass
from pathlib import Path

import gh_api as gh

JsonObject = dict[str, object]
Handler = Callable[[argparse.Namespace, Mapping[str, str]], int]

STATEMENT_TYPE: str = "https://in-toto.io/Statement/v1"
BUILD_TYPE: str = "https://actions.github.io/buildtypes/workflow/v1"
REQUIRED_ENV: tuple[str, ...] = (
    "GITHUB_SERVER_URL",
    "GITHUB_REPOSITORY",
    "GITHUB_REPOSITORY_ID",
    "GITHUB_REPOSITORY_OWNER_ID",
    "GITHUB_REF",
    "GITHUB_SHA",
    "GITHUB_WORKFLOW_REF",
    "GITHUB_EVENT_NAME",
    "GITHUB_RUN_ID",
    "GITHUB_RUN_ATTEMPT",
    "RUNNER_ENVIRONMENT",
)
SHA256: re.Pattern[str] = re.compile(r"[0-9a-f]{64}")
GIT_COMMIT: re.Pattern[str] = re.compile(r"[0-9a-f]{40}")
PURL_TYPE: re.Pattern[str] = re.compile(r"pkg:(?P<type>[a-z0-9.+-]+)/")
TOOL_FIELDS: int = 4
MAX_TOOLS: int = 16


class EvidenceError(Exception):
    """Evidence that cannot be built or does not pass its check."""


@dataclass(frozen=True)
class Tool:
    name: str
    version: str
    sha256: str
    url: str

    def descriptor(self) -> JsonObject:
        return {"name": f"{self.name}@{self.version}", "uri": self.url, "digest": {"sha256": self.sha256}}


@dataclass(frozen=True)
class RunContext:
    """The GitHub Actions run the evidence comes from."""

    server: str
    repository: str
    repository_id: str
    owner_id: str
    ref: str
    sha: str
    workflow_ref: str
    event_name: str
    run_id: str
    run_attempt: str
    runner_environment: str

    @classmethod
    def from_env(cls, env: Mapping[str, str]) -> RunContext:
        missing = [name for name in REQUIRED_ENV if not env.get(name)]
        if missing:
            raise EvidenceError(f"missing GitHub Actions variables: {', '.join(missing)}")
        run = cls(*(env[name] for name in REQUIRED_ENV))
        if not GIT_COMMIT.fullmatch(run.sha):
            raise EvidenceError(f"GITHUB_SHA is not a commit: {run.sha}")
        if not run.workflow_ref.startswith(f"{run.repository}/") or "@" not in run.workflow_ref:
            raise EvidenceError(f"GITHUB_WORKFLOW_REF is not a workflow in {run.repository}: {run.workflow_ref}")
        return run

    @property
    def repository_url(self) -> str:
        return f"{self.server}/{self.repository}"

    @property
    def workflow_path(self) -> str:
        return self.workflow_ref.split("@", 1)[0].removeprefix(f"{self.repository}/")


def parse_tools(text: str) -> list[Tool]:
    """The tools pinned in deploy/evidence/tools.txt: name version sha256 url."""
    tools: list[Tool] = []
    for number, line in enumerate(text.splitlines(), start=1):
        fields = line.split()
        if not fields or fields[0].startswith("#"):
            continue
        if len(fields) != TOOL_FIELDS or not SHA256.fullmatch(fields[2]):
            raise EvidenceError(f"line {number}: expected 'name version sha256 url'")
        tools.append(Tool(*fields))
    if not tools or len(tools) > MAX_TOOLS:
        raise EvidenceError(f"expected 1 to {MAX_TOOLS} tools, found {len(tools)}")
    return tools


def read_json(path: Path) -> object:
    try:
        document: object = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise EvidenceError(f"cannot read {path}: {error}") from error
    return document


def write_json(path: Path, document: JsonObject) -> None:
    path.write_text(json.dumps(document, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def sha256_of(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def build_predicate(run: RunContext, tools: Sequence[Tool]) -> JsonObject:
    """A SLSA v1 provenance predicate, in GitHub's workflow build type."""
    source: JsonObject = {"uri": f"git+{run.repository_url}@{run.ref}", "digest": {"gitCommit": run.sha}}
    return {
        "buildDefinition": {
            "buildType": BUILD_TYPE,
            "externalParameters": {
                "workflow": {"ref": run.ref, "repository": run.repository_url, "path": run.workflow_path},
            },
            "internalParameters": {
                "github": {
                    "event_name": run.event_name,
                    "repository_id": run.repository_id,
                    "repository_owner_id": run.owner_id,
                    "runner_environment": run.runner_environment,
                },
            },
            "resolvedDependencies": [source, *(tool.descriptor() for tool in tools)],
        },
        "runDetails": {
            "builder": {"id": f"{run.server}/actions/runner/{run.runner_environment}"},
            "metadata": {"invocationId": f"{run.repository_url}/actions/runs/{run.run_id}/attempts/{run.run_attempt}"},
        },
    }


def build_statement(name: str, digest: str, predicate_type: str, predicate: object) -> JsonObject:
    return {
        "_type": STATEMENT_TYPE,
        "subject": [{"name": name, "digest": {"sha256": digest}}],
        "predicateType": predicate_type,
        "predicate": predicate,
    }


def purl_counts(document: object) -> dict[str, int]:
    """How many components of each purl type a CycloneDX JSON SBOM lists."""
    if not isinstance(document, dict) or document.get("bomFormat") != "CycloneDX":
        raise EvidenceError("not a CycloneDX JSON SBOM")
    components: object = document.get("components")
    counts: dict[str, int] = {}
    if not isinstance(components, list):
        return counts
    for component in components:
        purl: object = component.get("purl") if isinstance(component, dict) else None
        match = PURL_TYPE.match(purl) if isinstance(purl, str) else None
        if match is not None:
            counts[match["type"]] = counts.get(match["type"], 0) + 1
    return counts


def publish_bundle(config: gh.ApiConfig, path: Path) -> int:
    bundle: object = read_json(path)
    if not isinstance(bundle, dict):
        raise EvidenceError(f"{path} is not a Sigstore bundle")
    result: object = gh.api_request(config, "POST", f"/repos/{config.repo}/attestations", {"bundle": bundle})
    if not isinstance(result, dict):
        raise EvidenceError(f"publishing {path.name}: unexpected response")
    return gh.require_int(result, "id", f"publishing {path.name}")


def run_predicate(args: argparse.Namespace, env: Mapping[str, str]) -> int:
    run = RunContext.from_env(env)
    tools_path: Path = args.tools
    try:
        tools = parse_tools(tools_path.read_text(encoding="utf-8"))
    except OSError as error:
        raise EvidenceError(f"cannot read {tools_path}: {error}") from error
    write_json(args.out, build_predicate(run, tools))
    sys.stdout.write(f"evidence: provenance predicate for {run.workflow_path} at {run.sha}\n")
    return 0


def run_statement(args: argparse.Namespace, _env: Mapping[str, str]) -> int:
    subject: Path = args.subject
    try:
        digest = sha256_of(subject)
    except OSError as error:
        raise EvidenceError(f"cannot read {subject}: {error}") from error
    write_json(args.out, build_statement(args.name, digest, args.predicate_type, read_json(args.predicate)))
    sys.stdout.write(f"evidence: {args.predicate_type} statement about {args.name} (sha256:{digest})\n")
    return 0


def run_check_sbom(args: argparse.Namespace, _env: Mapping[str, str]) -> int:
    counts = purl_counts(read_json(args.sbom))
    summary = " ".join(f"{kind}={count}" for kind, count in sorted(counts.items()))
    sys.stdout.write(f"sbom_check: {summary or 'no packages'}\n")
    missing = [kind for kind in args.require if kind not in counts]
    for kind in missing:
        sys.stdout.write(f"::error::the SBOM lists no pkg:{kind} packages\n")
    return 1 if missing else 0


def run_publish(args: argparse.Namespace, _env: Mapping[str, str]) -> int:
    config = gh.read_config()
    for path in args.bundles:
        attestation_id = publish_bundle(config, path)
        sys.stdout.write(f"evidence: published {path.name} as attestation {attestation_id}\n")
    return 0


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(prog="evidence", description="Build, check and publish the CI evidence.")
    commands = parser.add_subparsers(dest="command", required=True)
    predicate = commands.add_parser("predicate", help="write a SLSA v1 provenance predicate")
    predicate.add_argument("--tools", type=Path, required=True, help="the pinned tools (tools.txt)")
    predicate.add_argument("--out", type=Path, required=True)
    predicate.set_defaults(handler=run_predicate)
    statement = commands.add_parser("statement", help="wrap a predicate in an in-toto v1 statement")
    statement.add_argument("--subject", type=Path, required=True, help="the file the statement is about")
    statement.add_argument("--name", required=True, help="the subject's name in the statement")
    statement.add_argument("--predicate-type", required=True, help="the predicate's type URI")
    statement.add_argument("--predicate", type=Path, required=True, help="the predicate (JSON)")
    statement.add_argument("--out", type=Path, required=True)
    statement.set_defaults(handler=run_statement)
    check = commands.add_parser("check-sbom", help="require packages of each purl type")
    check.add_argument("sbom", type=Path, help="a CycloneDX JSON SBOM")
    check.add_argument("--require", nargs="+", required=True, metavar="TYPE", help="purl types, e.g. npm")
    check.set_defaults(handler=run_check_sbom)
    publish = commands.add_parser("publish", help="upload Sigstore bundles to the repository's attestations")
    publish.add_argument("bundles", type=Path, nargs="+", metavar="BUNDLE")
    publish.set_defaults(handler=run_publish)
    return parser.parse_args(argv)


def main(argv: Sequence[str], env: Mapping[str, str]) -> int:
    args = parse_args(argv)
    handler: Handler = args.handler
    try:
        return handler(args, env)
    except (EvidenceError, gh.ScriptError) as error:
        sys.stdout.write(f"::error::{error}\n")
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:], os.environ))
