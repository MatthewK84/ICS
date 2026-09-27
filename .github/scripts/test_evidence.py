"""Unit tests for evidence.py (stdlib unittest, no network)."""

from __future__ import annotations

import hashlib
import io
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import evidence as ev
import gh_api as gh

EXIT_CLEAN: int = 0
EXIT_FAILED: int = 1
COMMIT: str = "0123456789abcdef0123456789abcdef01234567"
SYFT_SHA256: str = "a" * 64
TOOLS_TEXT: str = f"# name version sha256 url\n\nsyft 1.52.0 {SYFT_SHA256} https://example.test/syft.tar.gz\n"


def github_env() -> dict[str, str]:
    """The default variables of a push to main, as GitHub Actions sets them."""
    return {
        "GITHUB_SERVER_URL": "https://github.com",
        "GITHUB_REPOSITORY": "MatthewK84/ICS",
        "GITHUB_REPOSITORY_ID": "101",
        "GITHUB_REPOSITORY_OWNER_ID": "202",
        "GITHUB_REF": "refs/heads/main",
        "GITHUB_SHA": COMMIT,
        "GITHUB_WORKFLOW_REF": "MatthewK84/ICS/.github/workflows/evidence.yml@refs/heads/main",
        "GITHUB_EVENT_NAME": "push",
        "GITHUB_RUN_ID": "303",
        "GITHUB_RUN_ATTEMPT": "2",
        "RUNNER_ENVIRONMENT": "github-hosted",
    }


def sbom() -> ev.JsonObject:
    """A CycloneDX SBOM with one or two packages of three purl types."""
    return {
        "bomFormat": "CycloneDX",
        "components": [
            {"name": "gtest", "purl": "pkg:conan/gtest@1.15.0"},
            {"name": "pytest", "purl": "pkg:pypi/pytest@9.1.1"},
            {"name": "vite", "purl": "pkg:npm/vite@7.0.0"},
            {"name": "react", "purl": "pkg:npm/react@19.0.0"},
            {"name": "conan.lock", "type": "file"},
            "not an object",
        ],
    }


def run_main(argv: list[str], env: dict[str, str] | None = None) -> tuple[int, str]:
    with patch("sys.stdout", new_callable=io.StringIO) as out:
        code = ev.main(argv, github_env() if env is None else env)
    return code, out.getvalue()


def write(folder: Path, name: str, text: str) -> Path:
    path = folder / name
    path.write_text(text, encoding="utf-8")
    return path


class ToolTests(unittest.TestCase):
    def test_parses_pins_and_skips_comments(self) -> None:
        self.assertEqual(
            ev.parse_tools(TOOLS_TEXT), [ev.Tool("syft", "1.52.0", SYFT_SHA256, "https://example.test/syft.tar.gz")]
        )

    def test_rejects_malformed_lines(self) -> None:
        for text in ("syft 1.52.0 https://example.test/syft\n", f"syft 1.52.0 {'z' * 64} https://example.test\n"):
            with self.subTest(text=text), self.assertRaisesRegex(ev.EvidenceError, "line 1"):
                ev.parse_tools(text)

    def test_requires_a_bounded_number_of_tools(self) -> None:
        with self.assertRaisesRegex(ev.EvidenceError, "found 0"):
            ev.parse_tools("# nothing pinned\n")
        line = f"tool 1 {SYFT_SHA256} https://example.test/tool\n"
        with self.assertRaisesRegex(ev.EvidenceError, f"found {ev.MAX_TOOLS + 1}"):
            ev.parse_tools(line * (ev.MAX_TOOLS + 1))


class RunContextTests(unittest.TestCase):
    def test_reads_the_workflow_path(self) -> None:
        run = ev.RunContext.from_env(github_env())
        self.assertEqual(run.workflow_path, ".github/workflows/evidence.yml")
        self.assertEqual(run.repository_url, "https://github.com/MatthewK84/ICS")

    def test_lists_every_missing_variable(self) -> None:
        env = {**github_env(), "GITHUB_SHA": "", "RUNNER_ENVIRONMENT": ""}
        env.pop("GITHUB_RUN_ID")
        with self.assertRaisesRegex(ev.EvidenceError, "GITHUB_SHA, GITHUB_RUN_ID, RUNNER_ENVIRONMENT"):
            ev.RunContext.from_env(env)

    def test_rejects_a_bad_commit_or_workflow(self) -> None:
        with self.assertRaisesRegex(ev.EvidenceError, "not a commit"):
            ev.RunContext.from_env({**github_env(), "GITHUB_SHA": "main"})
        for ref in ("Other/Repo/.github/workflows/x.yml@refs/heads/main", "MatthewK84/ICS/.github/workflows/x.yml"):
            with self.subTest(ref=ref), self.assertRaisesRegex(ev.EvidenceError, "not a workflow"):
                ev.RunContext.from_env({**github_env(), "GITHUB_WORKFLOW_REF": ref})


class PredicateTests(unittest.TestCase):
    def test_describes_the_run_in_githubs_build_type(self) -> None:
        predicate = ev.build_predicate(ev.RunContext.from_env(github_env()), ev.parse_tools(TOOLS_TEXT))
        self.assertEqual(
            predicate["buildDefinition"],
            {
                "buildType": "https://actions.github.io/buildtypes/workflow/v1",
                "externalParameters": {
                    "workflow": {
                        "ref": "refs/heads/main",
                        "repository": "https://github.com/MatthewK84/ICS",
                        "path": ".github/workflows/evidence.yml",
                    },
                },
                "internalParameters": {
                    "github": {
                        "event_name": "push",
                        "repository_id": "101",
                        "repository_owner_id": "202",
                        "runner_environment": "github-hosted",
                    },
                },
                "resolvedDependencies": [
                    {"uri": "git+https://github.com/MatthewK84/ICS@refs/heads/main", "digest": {"gitCommit": COMMIT}},
                    {
                        "name": "syft@1.52.0",
                        "uri": "https://example.test/syft.tar.gz",
                        "digest": {"sha256": SYFT_SHA256},
                    },
                ],
            },
        )
        self.assertEqual(
            predicate["runDetails"],
            {
                "builder": {"id": "https://github.com/actions/runner/github-hosted"},
                "metadata": {"invocationId": "https://github.com/MatthewK84/ICS/actions/runs/303/attempts/2"},
            },
        )

    def test_statement_names_one_subject(self) -> None:
        statement = ev.build_statement("ics.tar", "b" * 64, "https://cyclonedx.org/bom", {"x": 1})
        self.assertEqual(
            statement,
            {
                "_type": "https://in-toto.io/Statement/v1",
                "subject": [{"name": "ics.tar", "digest": {"sha256": "b" * 64}}],
                "predicateType": "https://cyclonedx.org/bom",
                "predicate": {"x": 1},
            },
        )


class SbomTests(unittest.TestCase):
    def test_counts_components_by_purl_type(self) -> None:
        self.assertEqual(ev.purl_counts(sbom()), {"conan": 1, "npm": 2, "pypi": 1})
        self.assertEqual(ev.purl_counts({"bomFormat": "CycloneDX", "components": "none"}), {})

    def test_rejects_other_documents(self) -> None:
        documents: tuple[object, ...] = ([], {"bomFormat": "SPDX"})
        for document in documents:
            with self.subTest(document=document), self.assertRaisesRegex(ev.EvidenceError, "CycloneDX"):
                ev.purl_counts(document)


class MainTests(unittest.TestCase):
    def test_predicate_and_statement_commands(self) -> None:
        with tempfile.TemporaryDirectory() as name:
            folder = Path(name)
            tools = write(folder, "tools.txt", TOOLS_TEXT)
            subject = write(folder, "ics.tar", "source")
            code, output = run_main(["predicate", "--tools", str(tools), "--out", str(folder / "p.json")])
            self.assertEqual(code, EXIT_CLEAN)
            self.assertIn(f"evidence.yml at {COMMIT}", output)
            argv = ["statement", "--subject", str(subject), "--name", "ics.tar", "--predicate-type", "urn:t"]
            code, output = run_main([*argv, "--predicate", str(folder / "p.json"), "--out", str(folder / "s.json")])
            statement: object = json.loads((folder / "s.json").read_text(encoding="utf-8"))
        digest = hashlib.sha256(b"source").hexdigest()
        self.assertEqual(code, EXIT_CLEAN)
        self.assertIn(f"sha256:{digest}", output)
        self.assertIsInstance(statement, dict)
        if isinstance(statement, dict):
            self.assertEqual(statement["subject"], [{"name": "ics.tar", "digest": {"sha256": digest}}])
            self.assertEqual(statement["predicate"]["buildDefinition"]["buildType"], ev.BUILD_TYPE)

    def test_missing_inputs_fail_cleanly(self) -> None:
        with tempfile.TemporaryDirectory() as name:
            folder = Path(name)
            absent = str(folder / "absent")
            out = str(folder / "out.json")
            write(folder, "bad.json", "{")
            self.assertEqual(run_main(["predicate", "--tools", absent, "--out", out])[0], EXIT_FAILED)
            self.assertIn("missing GitHub", run_main(["predicate", "--tools", absent, "--out", out], {})[1])
            argv = ["statement", "--subject", absent, "--name", "n", "--predicate-type", "t", "--predicate", absent]
            self.assertIn("cannot read", run_main([*argv, "--out", out])[1])
            self.assertIn("cannot read", run_main(["check-sbom", str(folder / "bad.json"), "--require", "npm"])[1])

    def test_check_sbom_requires_each_type(self) -> None:
        with tempfile.TemporaryDirectory() as name:
            full = write(Path(name), "sbom.json", json.dumps(sbom()))
            empty = write(Path(name), "empty.json", json.dumps({"bomFormat": "CycloneDX"}))
            clean = run_main(["check-sbom", str(full), "--require", "conan", "pypi", "npm"])
            failed = run_main(["check-sbom", str(full), "--require", "npm", "deb"])
            nothing = run_main(["check-sbom", str(empty), "--require", "npm"])
        self.assertEqual(clean, (EXIT_CLEAN, "sbom_check: conan=1 npm=2 pypi=1\n"))
        self.assertEqual(failed[0], EXIT_FAILED)
        self.assertIn("::error::the SBOM lists no pkg:deb packages", failed[1])
        self.assertIn("sbom_check: no packages", nothing[1])

    def test_publish_uploads_each_bundle(self) -> None:
        responses: list[object] = [{"id": 7}, {"id": 8}]
        with tempfile.TemporaryDirectory() as name:
            first = write(Path(name), "sbom.sigstore.json", '{"mediaType": "bundle"}')
            second = write(Path(name), "provenance.sigstore.json", '{"mediaType": "bundle"}')
            with (
                patch.dict("os.environ", {"GITHUB_TOKEN": "t", "GITHUB_REPOSITORY": "MatthewK84/ICS"}),
                patch.object(gh, "api_request", side_effect=responses) as request,
            ):
                code, output = run_main(["publish", str(first), str(second)])
        self.assertEqual(code, EXIT_CLEAN)
        self.assertIn("published sbom.sigstore.json as attestation 7", output)
        self.assertIn("published provenance.sigstore.json as attestation 8", output)
        config, method, path, payload = request.call_args_list[0].args
        self.assertEqual((config.repo, method, path), ("MatthewK84/ICS", "POST", "/repos/MatthewK84/ICS/attestations"))
        self.assertEqual(payload, {"bundle": {"mediaType": "bundle"}})

    def test_publish_rejects_bad_bundles_and_responses(self) -> None:
        with tempfile.TemporaryDirectory() as name:
            not_bundle = write(Path(name), "list.json", "[]")
            bundle = write(Path(name), "b.json", "{}")
            with (
                patch.dict("os.environ", {"GITHUB_TOKEN": "t", "GITHUB_REPOSITORY": "MatthewK84/ICS"}),
                patch.object(gh, "api_request", side_effect=[None, {"id": "x"}]),
            ):
                self.assertIn("not a Sigstore bundle", run_main(["publish", str(not_bundle)])[1])
                self.assertIn("unexpected response", run_main(["publish", str(bundle)])[1])
                self.assertIn("'id' must be an integer", run_main(["publish", str(bundle)])[1])


if __name__ == "__main__":
    unittest.main()
