"""Check the dependency register (ICS-010, docs/dependency-register.md).

  dependency_register.py check [--root DIR] [--register FILE]

Reads every dependency manifest under DIR without running a package manager
and fails unless each direct dependency has a complete, reviewed entry in the
register and every registered package is still used. Projects flagged as
single-maintainer are listed as notices. DoWI 8430.01 §3.5.i asks for this
review of governance, sustainment, license and foreign influence.

Manifests read: conanfile.py (literal requires), pyproject.toml,
requirements*.in, package.json, GitHub workflow and action files (uses: and
image:), Dockerfiles (FROM), apt-packages.txt and tools.txt pin lists.
"""

from __future__ import annotations

import argparse
import ast
import datetime
import json
import re
import sys
import tomllib
from collections.abc import Callable, Iterable, Mapping, Sequence
from dataclasses import dataclass
from pathlib import Path

JsonObject = dict[str, object]
Found = tuple[str, str]
Parser = Callable[[str], list[Found]]

ECOSYSTEMS: frozenset[str] = frozenset(
    {"apt", "conan", "container", "github-action", "npm", "pypi", "runtime", "tool"},
)
TEXT_FIELDS: tuple[str, ...] = (
    "name",
    "used_for",
    "license",
    "source",
    "governance",
    "sustainment",
    "foreign_influence",
    "maintainers",
    "prepared_by",
    "approved_by",
)
FLAG_FIELDS: tuple[str, ...] = ("ships", "single_maintainer")
CONAN_CALLS: frozenset[str] = frozenset({"requires", "tool_requires", "test_requires", "build_requires"})
NPM_SECTIONS: tuple[str, ...] = ("dependencies", "devDependencies", "optionalDependencies", "peerDependencies")
SKIPPED_DIRS: frozenset[str] = frozenset({".git", "node_modules", ".venv", ".pnpm-store", "__pycache__"})
MAX_FILES: int = 50_000
LICENSE: re.Pattern[str] = re.compile(r"[A-Za-z0-9.+\-]+(?: (?:AND|OR|WITH) [A-Za-z0-9.+\-]+)*")
PEP508_NAME: re.Pattern[str] = re.compile(r"\s*([A-Za-z0-9](?:[A-Za-z0-9._-]*[A-Za-z0-9])?)")
USES: re.Pattern[str] = re.compile(r"^\s*(?:-\s+)?uses:\s*['\"]?([^'\"\s#]+)", re.MULTILINE)
IMAGE: re.Pattern[str] = re.compile(r"^\s*(?:-\s+)?image:\s*['\"]?([^'\"\s#]+)", re.MULTILINE)
FROM: re.Pattern[str] = re.compile(r"^\s*FROM\s+(?:--\S+\s+)*(\S+)(?:\s+AS\s+(\S+))?", re.MULTILINE | re.IGNORECASE)


class RegisterError(Exception):
    """A manifest or register entry that cannot be read or is incomplete."""


@dataclass(frozen=True, order=True)
class Dependency:
    ecosystem: str
    name: str
    manifest: str


@dataclass(frozen=True)
class Project:
    name: str
    ecosystem: str
    packages: tuple[str, ...]
    single_maintainer: bool
    maintainers: str


@dataclass(frozen=True)
class Report:
    errors: tuple[str, ...]
    flagged: tuple[Project, ...]
    projects: int
    packages: int


def normalize(ecosystem: str, name: str) -> str:
    """The form a package name is compared in: PEP 503 for PyPI, lower case elsewhere."""
    if ecosystem == "pypi":
        return re.sub(r"[-_.]+", "-", name).lower()
    return name.lower()


def image_name(reference: str) -> str:
    """A container image reference without its tag or digest, with Docker Hub spelled out.

    The name must be written out. Only the tag or digest may come from a
    variable, as for ICS's own build images, whose tags are their versions
    (#159).
    """
    without_digest = reference.split("@", 1)[0]
    last_slash = without_digest.rfind("/")
    colon = without_digest.rfind(":")
    name = without_digest[:colon] if colon > last_slash else without_digest
    if "$" in name:
        raise RegisterError(f"image '{reference}' must be written out, not built from variables")
    parts = name.split("/")
    if len(parts) == 1:
        return f"docker.io/library/{name}".lower()
    if "." in parts[0] or ":" in parts[0] or parts[0] == "localhost":
        return name.lower()
    return f"docker.io/{name}".lower()


def conan_reference(node: ast.expr, line: int) -> str:
    if not isinstance(node, ast.Constant) or not isinstance(node.value, str):
        raise RegisterError(f"conanfile.py:{line}: a requirement must be a literal 'name/version' string")
    return node.value.split("/", 1)[0]


def conan_attribute(node: ast.Assign) -> list[str]:
    """Requirements written as class attributes: requires = "a/1", "b/2"."""
    names = [target.id for target in node.targets if isinstance(target, ast.Name)]
    if not any(name in CONAN_CALLS for name in names):
        return []
    values = node.value.elts if isinstance(node.value, ast.Tuple | ast.List) else [node.value]
    return [conan_reference(value, node.lineno) for value in values]


def conan_call(node: ast.Call) -> list[str]:
    """Requirements written as calls: self.requires("a/1")."""
    if not isinstance(node.func, ast.Attribute) or node.func.attr not in CONAN_CALLS:
        return []
    if not node.args:
        raise RegisterError(f"conanfile.py:{node.lineno}: {node.func.attr}() needs a reference")
    return [conan_reference(node.args[0], node.lineno)]


def conan_requires(text: str) -> list[Found]:
    found: list[Found] = []
    for node in ast.walk(ast.parse(text)):
        if isinstance(node, ast.Assign):
            found.extend(("conan", name) for name in conan_attribute(node))
        elif isinstance(node, ast.Call):
            found.extend(("conan", name) for name in conan_call(node))
    return found


def requirement_name(requirement: str) -> str:
    match = PEP508_NAME.match(requirement)
    if match is None:
        raise RegisterError(f"cannot read the package name in requirement '{requirement}'")
    return match.group(1)


def strings_in(value: object, where: str) -> list[str]:
    """The requirement strings in a TOML list; include-group tables are skipped."""
    if value is None:
        return []
    if not isinstance(value, list):
        raise RegisterError(f"{where} must be a list")
    return [item for item in value if isinstance(item, str)]


def pyproject_requirements(text: str) -> list[Found]:
    document = tomllib.loads(text)
    project = document.get("project", {})
    requirements = strings_in(project.get("dependencies"), "project.dependencies")
    for group, items in project.get("optional-dependencies", {}).items():
        requirements.extend(strings_in(items, f"project.optional-dependencies.{group}"))
    for group, items in document.get("dependency-groups", {}).items():
        requirements.extend(strings_in(items, f"dependency-groups.{group}"))
    build = document.get("build-system", {})
    requirements.extend(strings_in(build.get("requires"), "build-system.requires"))
    found: list[Found] = [("pypi", requirement_name(requirement)) for requirement in requirements]
    if "requires-python" in project:
        found.append(("runtime", "python"))
    return found


def requirements_in(text: str) -> list[Found]:
    found: list[Found] = []
    for number, raw in enumerate(text.splitlines(), start=1):
        line = raw.split("#", 1)[0].strip()
        if not line or line.startswith(("-r ", "-c ", "--requirement", "--constraint")):
            continue
        if line.startswith("-"):
            raise RegisterError(f"line {number}: option '{line}' is not supported; name the package")
        found.append(("pypi", requirement_name(line)))
    return found


def package_json(text: str) -> list[Found]:
    document: object = json.loads(text)
    if not isinstance(document, dict):
        raise RegisterError("package.json must hold a JSON object")
    found: list[Found] = []
    for section in NPM_SECTIONS:
        entries: object = document.get(section, {})
        if not isinstance(entries, dict):
            raise RegisterError(f"package.json: '{section}' must be an object")
        found.extend(("npm", name) for name, spec in entries.items() if not str(spec).startswith("workspace:"))
    manager: object = document.get("packageManager")
    if isinstance(manager, str):
        found.append(("runtime", manager.split("@", 1)[0]))
    engines: object = document.get("engines", {})
    if isinstance(engines, dict):
        found.extend(("runtime", name) for name in engines)
    return found


def workflow_dependencies(text: str) -> list[Found]:
    found: list[Found] = []
    for reference in USES.findall(text):
        if reference.startswith("./"):
            continue
        if reference.startswith("docker://"):
            found.append(("container", image_name(reference.removeprefix("docker://"))))
            continue
        owner_repo = "/".join(reference.split("@", 1)[0].split("/")[:2])
        found.append(("github-action", owner_repo))
    found.extend(("container", image_name(reference)) for reference in IMAGE.findall(text))
    return found


def dockerfile_images(text: str) -> list[Found]:
    stages: set[str] = set()
    found: list[Found] = []
    for image, alias in FROM.findall(text):
        if image.lower() != "scratch" and image.lower() not in stages:
            found.append(("container", image_name(image)))
        if alias:
            stages.add(alias.lower())
    return found


def first_fields(ecosystem: str) -> Parser:
    """A parser for lists with one package per line, its name first; '#' starts a comment."""

    def parse(text: str) -> list[Found]:
        lines = (raw.split("#", 1)[0].split() for raw in text.splitlines())
        return [(ecosystem, fields[0].split("=", 1)[0]) for fields in lines if fields]

    return parse


def parser_for(relative: str) -> Parser | None:
    """The parser for a manifest, by its path relative to the root, or None."""
    name = relative.rsplit("/", 1)[-1]
    is_workflow = relative.startswith(".github/workflows/") and name.endswith((".yml", ".yaml"))
    matches: tuple[tuple[bool, Parser], ...] = (
        (name == "conanfile.py", conan_requires),
        (name == "pyproject.toml", pyproject_requirements),
        (name.startswith("requirements") and name.endswith(".in"), requirements_in),
        (name == "package.json", package_json),
        (is_workflow or name in ("action.yml", "action.yaml"), workflow_dependencies),
        (name == "Dockerfile" or name.startswith("Dockerfile.") or name.endswith(".Dockerfile"), dockerfile_images),
        (name == "apt-packages.txt", first_fields("apt")),
        (name == "tools.txt", first_fields("tool")),
    )
    return next((parser for matched, parser in matches if matched), None)


def find_manifests(root: Path) -> list[tuple[str, Parser]]:
    """Every manifest under root, as (relative path, parser), skipping installed packages."""
    manifests: list[tuple[str, Parser]] = []
    pending: list[Path] = [root]
    seen = 0
    while pending:
        directory = pending.pop()
        for entry in sorted(directory.iterdir()):
            seen += 1
            if seen > MAX_FILES:
                raise RegisterError(f"more than {MAX_FILES} files under {root}")
            if entry.is_dir() and not entry.is_symlink():
                if entry.name not in SKIPPED_DIRS:
                    pending.append(entry)
                continue
            relative = entry.relative_to(root).as_posix()
            parser = parser_for(relative)
            if parser is not None:
                manifests.append((relative, parser))
    return sorted(manifests, key=lambda manifest: manifest[0])


def collect(root: Path, first_party: Sequence[str]) -> list[Dependency]:
    dependencies: list[Dependency] = []
    for relative, parser in find_manifests(root):
        try:
            found = parser((root / relative).read_text(encoding="utf-8"))
        except (OSError, ValueError, SyntaxError, RegisterError) as error:
            raise RegisterError(f"{relative}: {error}") from error
        for ecosystem, name in found:
            if not name.lower().startswith(tuple(first_party)):
                dependencies.append(Dependency(ecosystem, normalize(ecosystem, name), relative))
    return sorted(set(dependencies))


def field_errors(entry: Mapping[str, object], label: str) -> list[str]:
    errors = [f"{label}: '{key}' must be non-empty text" for key in TEXT_FIELDS if not text_value(entry, key)]
    errors.extend(f"{label}: '{key}' must be true or false" for key in FLAG_FIELDS if not flag_value(entry, key))
    if not isinstance(entry.get("reviewed"), datetime.date):
        errors.append(f"{label}: 'reviewed' must be a date, for example 2026-09-27")
    if entry.get("ecosystem") not in ECOSYSTEMS:
        errors.append(f"{label}: 'ecosystem' must be one of {', '.join(sorted(ECOSYSTEMS))}")
    if text_value(entry, "license") and not LICENSE.fullmatch(str(entry["license"])):
        errors.append(f"{label}: 'license' must be an SPDX expression, for example 'MIT' or 'Apache-2.0 OR MIT'")
    packages: object = entry.get("packages")
    if not isinstance(packages, list) or not packages or not all(isinstance(p, str) and p for p in packages):
        errors.append(f"{label}: 'packages' must be a non-empty list of package names")
    return errors


def text_value(entry: Mapping[str, object], key: str) -> bool:
    value: object = entry.get(key)
    return isinstance(value, str) and bool(value.strip())


def flag_value(entry: Mapping[str, object], key: str) -> bool:
    return isinstance(entry.get(key), bool)


def to_project(entry: Mapping[str, object]) -> Project:
    ecosystem = str(entry["ecosystem"])
    packages = entry["packages"]
    names = tuple(normalize(ecosystem, str(p)) for p in packages) if isinstance(packages, list) else ()
    return Project(str(entry["name"]), ecosystem, names, entry["single_maintainer"] is True, str(entry["maintainers"]))


@dataclass(frozen=True)
class Register:
    first_party: tuple[str, ...]
    projects: tuple[Project, ...]
    errors: tuple[str, ...]


def load_register(text: str) -> Register:
    document = tomllib.loads(text)
    settings: object = document.get("register", {})
    first_party: object = settings.get("first_party", []) if isinstance(settings, dict) else []
    if not isinstance(first_party, list) or not all(isinstance(prefix, str) for prefix in first_party):
        raise RegisterError("register.first_party must be a list of name prefixes")
    entries: object = document.get("project", [])
    if not isinstance(entries, list):
        raise RegisterError("the register must hold [[project]] entries")
    projects: list[Project] = []
    errors: list[str] = []
    for index, entry in enumerate(entries, start=1):
        if not isinstance(entry, dict):
            raise RegisterError(f"project {index} must be a table")
        problems = field_errors(entry, f"project {index} ({entry.get('name', 'unnamed')})")
        errors.extend(problems)
        if not problems:
            projects.append(to_project(entry))
    return Register(tuple(prefix.lower() for prefix in first_party), tuple(projects), tuple(errors))


def registered_packages(projects: Iterable[Project]) -> tuple[dict[Found, str], list[str]]:
    owners: dict[Found, str] = {}
    errors: list[str] = []
    for project in projects:
        for package in project.packages:
            key = (project.ecosystem, package)
            if key in owners:
                errors.append(
                    f"{project.ecosystem} package '{package}' is registered twice: {owners[key]}, {project.name}"
                )
            owners[key] = project.name
    return owners, errors


def compare(dependencies: Sequence[Dependency], register: Register) -> Report:
    owners, errors = registered_packages(register.projects)
    errors = [*register.errors, *errors]
    used: set[Found] = set()
    for dependency in dependencies:
        key = (dependency.ecosystem, dependency.name)
        used.add(key)
        if key not in owners:
            errors.append(
                f"{dependency.manifest}: unregistered {dependency.ecosystem} dependency '{dependency.name}'; "
                "add it to docs/dependency-register.toml"
            )
    errors.extend(
        f"stale register entry: {ecosystem} package '{name}' ({owner}) is not used by any manifest"
        for (ecosystem, name), owner in sorted(owners.items())
        if (ecosystem, name) not in used
    )
    flagged = tuple(project for project in register.projects if project.single_maintainer)
    return Report(tuple(errors), flagged, len(register.projects), len(owners))


def print_report(report: Report) -> int:
    for error in report.errors:
        sys.stdout.write(f"::error::{error}\n")
    for project in report.flagged:
        sys.stdout.write(f"::notice::single-maintainer project: {project.name} ({project.maintainers})\n")
    sys.stdout.write(
        f"dependency_register: {report.projects} projects, {report.packages} packages, "
        f"{len(report.flagged)} single-maintainer, {len(report.errors)} error(s)\n"
    )
    return 1 if report.errors else 0


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(prog="dependency_register", description="Check the dependency register.")
    commands = parser.add_subparsers(dest="command", required=True)
    check = commands.add_parser("check", help="fail on unregistered, stale or incomplete entries")
    check.add_argument("--root", type=Path, default=Path(), help="the repository root (default: .)")
    check.add_argument("--register", type=Path, help="the register (default: ROOT/docs/dependency-register.toml)")
    return parser.parse_args(argv)


def main(argv: Sequence[str]) -> int:
    args = parse_args(argv)
    root: Path = args.root
    register_path: Path = args.register or root / "docs" / "dependency-register.toml"
    try:
        register = load_register(register_path.read_text(encoding="utf-8"))
        dependencies = collect(root, register.first_party)
    except (OSError, ValueError, RegisterError) as error:
        sys.stdout.write(f"::error::{error}\n")
        return 1
    return print_report(compare(dependencies, register))


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
