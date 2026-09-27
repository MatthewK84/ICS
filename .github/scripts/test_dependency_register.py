"""Unit tests for dependency_register.py (stdlib unittest, no network)."""

from __future__ import annotations

import io
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import dependency_register as dr

EXIT_CLEAN: int = 0
EXIT_FAILED: int = 1


def entry(name: str, ecosystem: str, packages: str, *, single: str = "false") -> str:
    """One complete [[project]] table in TOML."""
    return f"""
[[project]]
name = "{name}"
ecosystem = "{ecosystem}"
packages = [{packages}]
used_for = "tests"
ships = false
license = "MIT"
source = "https://example.test/{name}"
governance = "a team"
sustainment = "active"
foreign_influence = "none known"
single_maintainer = {single}
maintainers = "someone"
reviewed = 2026-09-27
prepared_by = "Claude Code"
approved_by = "MatthewK84"
"""


def register_text() -> str:
    return (
        '[register]\nfirst_party = ["@ics/", "ghcr.io/matthewk84/"]\n'
        + entry("Vite", "npm", '"vite"')
        + entry("pnpm", "runtime", '"pnpm"', single="true")
        + entry("Checkout", "github-action", '"actions/checkout"')
    )


def write_tree(root: Path, files: dict[str, str]) -> None:
    for relative, text in files.items():
        path = root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")


def run_main(argv: list[str]) -> tuple[int, str]:
    with patch("sys.stdout", new_callable=io.StringIO) as out:
        code = dr.main(argv)
    return code, out.getvalue()


PACKAGE_JSON: str = '{"packageManager": "pnpm@12.6.0+sha512.abc", "devDependencies": {"vite": "8.3.1"}}'
WORKFLOW: str = (
    "steps:\n  - uses: actions/checkout@0123 # v6\n    with:\n      image: ghcr.io/matthewk84/ics-cpp:main\n"
)


class NameTests(unittest.TestCase):
    def test_normalizes_by_ecosystem(self) -> None:
        self.assertEqual(dr.normalize("pypi", "Pytest_Cov"), "pytest-cov")
        self.assertEqual(dr.normalize("pypi", "zope.interface"), "zope-interface")
        self.assertEqual(dr.normalize("npm", "@Types/Node"), "@types/node")

    def test_spells_out_image_names(self) -> None:
        cases = {
            "ubuntu:24.04@sha256:0081": "docker.io/library/ubuntu",
            "nvidia/cuda:12.9.1-devel-ubuntu24.04": "docker.io/nvidia/cuda",
            "mcr.microsoft.com/playwright:v1.63.0-noble@sha256:eff1": "mcr.microsoft.com/playwright",
            "localhost:5000/tool:1": "localhost:5000/tool",
            "localhost/tool": "localhost/tool",
            "GHCR.io/Owner/Image": "ghcr.io/owner/image",
        }
        for reference, expected in cases.items():
            with self.subTest(reference=reference):
                self.assertEqual(dr.image_name(reference), expected)

    def test_rejects_images_built_from_variables(self) -> None:
        with self.assertRaisesRegex(dr.RegisterError, "written out"):
            dr.image_name("${BASE}:latest")


class ConanTests(unittest.TestCase):
    def test_reads_calls_and_attributes(self) -> None:
        text = (
            "class C:\n"
            '    requires = "zlib/1.3", "fmt/10.2.1"\n'
            '    tool_requires = ["cmake/3.28.1"]\n'
            '    test_requires = "catch2/3.5.0"\n'
            "    other = 1\n"
            "    def requirements(self):\n"
            '        self.requires("gtest/1.15.0")\n'
            '        self.output.info("not a requirement")\n'
            '        print("nor is this")\n'
        )
        self.assertEqual(
            sorted(dr.conan_requires(text)),
            [("conan", "catch2"), ("conan", "cmake"), ("conan", "fmt"), ("conan", "gtest"), ("conan", "zlib")],
        )

    def test_rejects_requirements_it_cannot_read(self) -> None:
        for text in ("self.requires(name)\n", "self.requires()\n", "requires = VERSION\n"):
            with self.subTest(text=text), self.assertRaises(dr.RegisterError):
                dr.conan_requires(text)


class PythonTests(unittest.TestCase):
    def test_reads_every_requirement_table(self) -> None:
        text = (
            '[project]\nrequires-python = "==3.12.*"\ndependencies = ["numpy>=2"]\n'
            "[project.optional-dependencies]\nplot = [\"Matplotlib[qt] ; python_version>'3'\"]\n"
            '[dependency-groups]\ndev = ["pytest==9.1.1", {include-group = "lint"}]\nlint = ["ruff"]\n'
            '[build-system]\nrequires = ["hatchling"]\n'
        )
        self.assertEqual(
            dr.pyproject_requirements(text),
            [
                ("pypi", "numpy"),
                ("pypi", "Matplotlib"),
                ("pypi", "pytest"),
                ("pypi", "ruff"),
                ("pypi", "hatchling"),
                ("runtime", "python"),
            ],
        )

    def test_rejects_malformed_tables(self) -> None:
        with self.assertRaisesRegex(dr.RegisterError, "must be a list"):
            dr.pyproject_requirements('[project]\ndependencies = "numpy"\n')
        with self.assertRaisesRegex(dr.RegisterError, "package name"):
            dr.requirement_name("  ==1.0")

    def test_reads_requirements_in_files(self) -> None:
        text = "# comment\nconan==2.27.0  # pinned\n\n-r base.in\n-c constraints.txt\nsetuptools\n"
        self.assertEqual(dr.requirements_in(text), [("pypi", "conan"), ("pypi", "setuptools")])
        with self.assertRaisesRegex(dr.RegisterError, "line 1: option"):
            dr.requirements_in("-e git+https://example.test/x.git\n")


class NpmTests(unittest.TestCase):
    def test_reads_sections_manager_and_engines(self) -> None:
        text = (
            '{"packageManager": "pnpm@12.6.0+sha512.abc", "engines": {"node": "24.20.0"},'
            ' "dependencies": {"react": "18.3.1", "@ics/shared": "workspace:*"},'
            ' "devDependencies": {"vite": "8.3.1"}, "peerDependencies": {"eslint": "^10"}}'
        )
        self.assertEqual(
            sorted(dr.package_json(text)),
            [("npm", "eslint"), ("npm", "react"), ("npm", "vite"), ("runtime", "node"), ("runtime", "pnpm")],
        )

    def test_rejects_malformed_package_json(self) -> None:
        with self.assertRaisesRegex(dr.RegisterError, "JSON object"):
            dr.package_json("[]")
        with self.assertRaisesRegex(dr.RegisterError, "'dependencies' must be an object"):
            dr.package_json('{"dependencies": []}')


class CiTests(unittest.TestCase):
    def test_reads_actions_and_images_from_workflows(self) -> None:
        text = (
            "jobs:\n  a:\n    container:\n      image: 'mcr.microsoft.com/playwright:v1@sha256:ab'\n"
            "    steps:\n"
            "      - uses: github/codeql-action/init@2892aa5e  # v4\n"
            '      - uses: "actions/checkout@d234"\n'
            "      - uses: ./.github/actions/local\n"
            "      - uses: docker://alpine:3.20\n"
            "    env:\n      IMAGE: ghcr.io/example/not-an-image-key\n"
        )
        self.assertEqual(
            sorted(dr.workflow_dependencies(text)),
            [
                ("container", "docker.io/library/alpine"),
                ("container", "mcr.microsoft.com/playwright"),
                ("github-action", "actions/checkout"),
                ("github-action", "github/codeql-action"),
            ],
        )

    def test_reads_dockerfile_bases_but_not_stages(self) -> None:
        text = (
            "FROM --platform=linux/amd64 ubuntu:24.04@sha256:0081 AS build\n"
            "RUN make\n"
            "from build as test\n"
            "FROM scratch\n"
            "FROM nvidia/cuda:12.9.1-devel-ubuntu24.04\n"
        )
        self.assertEqual(
            dr.dockerfile_images(text),
            [("container", "docker.io/library/ubuntu"), ("container", "docker.io/nvidia/cuda")],
        )

    def test_reads_plain_lists(self) -> None:
        parse = dr.first_fields("apt")
        self.assertEqual(
            parse("# header\ngcc-13\nca-certificates=20240203  # pinned\n\n"),
            [
                ("apt", "gcc-13"),
                ("apt", "ca-certificates"),
            ],
        )


class ManifestTests(unittest.TestCase):
    def test_matches_manifests_by_path(self) -> None:
        expected = {
            "cpp/conanfile.py": dr.conan_requires,
            "python/pyproject.toml": dr.pyproject_requirements,
            "deploy/toolchain/requirements-conan.in": dr.requirements_in,
            "web/package.json": dr.package_json,
            ".github/workflows/ci.yaml": dr.workflow_dependencies,
            "tools/action.yml": dr.workflow_dependencies,
            "deploy/toolchain/Dockerfile.cpp": dr.dockerfile_images,
            "images/app.Dockerfile": dr.dockerfile_images,
            "Dockerfile": dr.dockerfile_images,
        }
        for relative, parser in expected.items():
            with self.subTest(relative=relative):
                self.assertIs(dr.parser_for(relative), parser)
        for relative in ("docs/notes.yml", "python/requirements.txt", "tools.md", "README.md"):
            with self.subTest(relative=relative):
                self.assertIsNone(dr.parser_for(relative))
        self.assertIsNotNone(dr.parser_for("deploy/evidence/tools.txt"))
        self.assertIsNotNone(dr.parser_for("proto/tools.txt"))
        self.assertIsNotNone(dr.parser_for("deploy/toolchain/apt-packages.txt"))

    def test_finds_manifests_but_skips_installed_packages(self) -> None:
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            write_tree(
                root,
                {
                    "web/package.json": PACKAGE_JSON,
                    "web/node_modules/vite/package.json": '{"dependencies": {"esbuild": "1"}}',
                    "python/.venv/lib/pyproject.toml": "[project]\n",
                    "README.md": "text",
                },
            )
            self.assertEqual([relative for relative, _ in dr.find_manifests(root)], ["web/package.json"])
            with patch.object(dr, "MAX_FILES", 2), self.assertRaisesRegex(dr.RegisterError, "more than 2 files"):
                dr.find_manifests(root)

    def test_collects_dependencies_without_first_party_ones(self) -> None:
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            write_tree(root, {"web/package.json": PACKAGE_JSON, ".github/workflows/ci.yml": WORKFLOW})
            found = dr.collect(root, ("ghcr.io/matthewk84/",))
            write_tree(root, {"web/package.json": "{"})
            with self.assertRaisesRegex(dr.RegisterError, "^web/package.json: "):
                dr.collect(root, ())
        self.assertEqual(
            found,
            [
                dr.Dependency("github-action", "actions/checkout", ".github/workflows/ci.yml"),
                dr.Dependency("npm", "vite", "web/package.json"),
                dr.Dependency("runtime", "pnpm", "web/package.json"),
            ],
        )


class RegisterTests(unittest.TestCase):
    def test_loads_complete_entries(self) -> None:
        register = dr.load_register(register_text())
        self.assertEqual(register.first_party, ("@ics/", "ghcr.io/matthewk84/"))
        self.assertEqual(register.errors, ())
        self.assertEqual([project.name for project in register.projects], ["Vite", "pnpm", "Checkout"])
        self.assertTrue(register.projects[1].single_maintainer)

    def test_reports_every_incomplete_field(self) -> None:
        text = (
            '[[project]]\nname = "Bad"\necosystem = "maven"\npackages = []\nships = "no"\n'
            'license = "MIT, BSD"\nreviewed = "yesterday"\n'
        )
        errors = "\n".join(dr.load_register(text).errors)
        for fragment in (
            "'used_for' must be non-empty text",
            "'approved_by' must be non-empty text",
            "'ships' must be true or false",
            "'single_maintainer' must be true or false",
            "'reviewed' must be a date",
            "'ecosystem' must be one of",
            "'license' must be an SPDX expression",
            "'packages' must be a non-empty list",
        ):
            with self.subTest(fragment=fragment):
                self.assertIn(f"project 1 (Bad): {fragment}", errors)

    def test_rejects_a_malformed_register(self) -> None:
        cases = {
            '[register]\nfirst_party = "@ics/"\n': "first_party",
            "project = 1\n": r"\[\[project\]\]",
            "project = [1]\n": "project 1 must be a table",
        }
        for text, message in cases.items():
            with self.subTest(text=text), self.assertRaisesRegex(dr.RegisterError, message):
                dr.load_register(text)

    def test_compares_manifests_with_the_register(self) -> None:
        register = dr.load_register(register_text() + entry("Vite again", "npm", '"VITE"'))
        found = [
            dr.Dependency("npm", "vite", "web/package.json"),
            dr.Dependency("npm", "left-pad", "web/package.json"),
            dr.Dependency("runtime", "pnpm", "web/package.json"),
        ]
        report = dr.compare(found, register)
        self.assertEqual(
            report.errors,
            (
                "npm package 'vite' is registered twice: Vite, Vite again",
                "web/package.json: unregistered npm dependency 'left-pad'; add it to docs/dependency-register.toml",
                "stale register entry: github-action package 'actions/checkout' (Checkout) is not used by any manifest",
            ),
        )
        self.assertEqual([project.name for project in report.flagged], ["pnpm"])
        self.assertEqual((report.projects, report.packages), (4, 3))


class MainTests(unittest.TestCase):
    def test_passes_when_every_dependency_is_registered(self) -> None:
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            write_tree(
                root,
                {
                    "web/package.json": PACKAGE_JSON,
                    ".github/workflows/ci.yml": WORKFLOW,
                    "docs/dependency-register.toml": register_text(),
                },
            )
            code, output = run_main(["check", "--root", name])
        self.assertEqual(code, EXIT_CLEAN)
        self.assertIn("::notice::single-maintainer project: pnpm (someone)", output)
        self.assertIn("dependency_register: 3 projects, 3 packages, 1 single-maintainer, 0 error(s)", output)

    def test_fails_on_a_new_unregistered_dependency(self) -> None:
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            new_package = PACKAGE_JSON.replace('"vite": "8.3.1"', '"vite": "8.3.1", "left-pad": "1.3.0"')
            write_tree(root, {"web/package.json": new_package, ".github/workflows/ci.yml": WORKFLOW})
            write_tree(root, {"register.toml": register_text()})
            code, output = run_main(["check", "--root", name, "--register", str(root / "register.toml")])
        self.assertEqual(code, EXIT_FAILED)
        self.assertIn("::error::web/package.json: unregistered npm dependency 'left-pad'", output)

    def test_fails_cleanly_without_a_register(self) -> None:
        with tempfile.TemporaryDirectory() as name:
            code, output = run_main(["check", "--root", name])
        self.assertEqual(code, EXIT_FAILED)
        self.assertIn("::error::", output)


if __name__ == "__main__":
    unittest.main()
