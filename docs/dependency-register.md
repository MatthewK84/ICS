# Dependency register

[`dependency-register.toml`](dependency-register.toml) lists every direct dependency of ICS, with a review of its governance, sustainment, license and foreign influence, as [DoWI 8430.01](https://www.esd.whs.mil/Portals/54/Documents/DD/issuances/dodi/843001p.pdf) §3.5.i requires ([ICS-010](https://github.com/MatthewK84/ICS/issues/10)). The required "Dependency register" check enforces it on every pull request.

## What counts as a direct dependency

A direct dependency is anything the repository names that ICS builds with, tests with, runs on or ships. The check reads these manifests:

| Ecosystem | Read from |
|---|---|
| `conan` | `requires` calls and attributes in `conanfile.py`; the reference must be a literal string |
| `pypi` | `pyproject.toml` (dependencies, optional dependencies, dependency groups and build requirements) and `requirements*.in` |
| `npm` | every `package.json`: dependencies, devDependencies, optionalDependencies and peerDependencies |
| `runtime` | `packageManager` and `engines` in `package.json`, and `requires-python` in `pyproject.toml` |
| `github-action` | `uses:` in `.github/workflows/*.yml` and `action.yml` files |
| `container` | `image:` in workflows and action files, `docker://` actions, and `FROM` in Dockerfiles |
| `apt` | `apt-packages.txt`, the Ubuntu packages in the toolchain images |
| `tool` | `tools.txt` files: the pinned binaries of the evidence pipeline (`deploy/evidence/`) and the protobuf pipeline (`proto/`), the pinned data in the toolchain images (`deploy/toolchain/`), the autopilot sources of the SITL rig (`deploy/sitl/image/`), and files copied from other projects (`proto/third_party/`, `cpp/sapient/test/samples/`) |

Transitive dependencies are not registered: the lockfiles pin them and the SBOMs list them ([ICS-009](../deploy/evidence/README.md)). Packages and images whose names start with a `first_party` prefix in the register, such as `@ics/` and `ghcr.io/matthewk84/`, are ICS's own. Workspace references (`workspace:`) and local actions (`./`) are skipped too.

The check does not see a dependency fetched some other way, for example by CMake `FetchContent`, a `curl` in a script or a `docker run` of an image. Don't add dependencies that way: declare them in a manifest.

## Fields

Each `[[project]]` entry covers one project, which may publish several packages in one ecosystem (React publishes `react` and `react-dom`).

| Field | Content |
|---|---|
| `name` | The project's name |
| `ecosystem` | One of the ecosystems above |
| `packages` | The package, action or image names the manifests use |
| `used_for` | What ICS uses it for |
| `ships` | `true` if it is part of software ICS delivers; `false` for build, test and CI tools |
| `license` | An SPDX license expression; use `LicenseRef-…` for a license SPDX does not list |
| `source` | Where its source code lives |
| `governance` | Who controls the project and how decisions and releases are made |
| `sustainment` | How active it is, how ICS pins it, and what would replace it |
| `foreign_influence` | The controlling organization and its jurisdiction, or what could not be established |
| `single_maintainer` | `true` when one person does most of the maintenance and no one else is set up to take over |
| `maintainers` | Who maintains it: the basis for `single_maintainer` |
| `reviewed` | The date of the review, for example `2026-09-27` |
| `prepared_by` | Who researched and wrote the entry, including an AI tool |
| `approved_by` | The GitHub user who approved it; merging the pull request is the approval |

Entries record public information as of `reviewed`. The register tracks projects, not versions, so upgrading a pinned version needs no new review. Review an entry again when the project changes hands, licenses or governance.

## Adding or removing a dependency

1. Add the dependency to its manifest, and update the lockfile with the repository's tooling.
2. Add or extend a `[[project]]` entry in the same pull request, with every field filled in. Prefer primary sources: the project's governance and security pages, its license file and its registry metadata.
3. Run `.github/scripts/check_dependency_register.sh` from the repository root. It checks the register, then proves that a new unregistered dependency is still rejected.

When a dependency is removed, delete its package from `packages`, or the whole entry if nothing is left. The check fails on an entry that no manifest uses any more.

## What the check reports

- **Errors**, which fail the check:
  - a dependency with no entry;
  - an entry with a missing or malformed field;
  - a package registered twice;
  - a registered package that no manifest uses.
- **Notices**, which don't fail the check: each project marked `single_maintainer`, so the risk stays visible.
