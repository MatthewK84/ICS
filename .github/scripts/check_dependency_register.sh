#!/usr/bin/env bash
# Check the dependency register (ICS-010), then prove the check rejects a new
# unregistered dependency ("Done when"). Three copies of the repository each
# gain one dependency: an npm package, a GitHub Action and an Ubuntu package.
# The check must fail on each copy and name the new dependency.
#
# Usage: .github/scripts/check_dependency_register.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
readonly ROOT
readonly CHECK="${ROOT}/.github/scripts/dependency_register.py"
readonly UNVETTED_SHA="0000000000000000000000000000000000000000"
WORK="$(mktemp -d)"
readonly WORK
trap 'rm -rf "${WORK}"' EXIT

# Copy the files git would commit (tracked and new, not ignored) into $1.
copy_tree() {
  mkdir -p "$1"
  git -C "${ROOT}" ls-files -z --cached --others --exclude-standard |
    tar --null --ignore-failed-read -C "${ROOT}" -T - -cf - | tar -xf - -C "$1"
}

add_npm_package() {
  python3 - "$1/web/package.json" <<'EOF'
import json
import sys
from pathlib import Path

path = Path(sys.argv[1])
document = json.loads(path.read_text(encoding="utf-8"))
document["devDependencies"]["left-pad"] = "1.3.0"
path.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")
EOF
}

add_github_action() {
  cat >"$1/.github/workflows/seeded.yml" <<EOF
on: push
jobs:
  seeded:
    runs-on: ubuntu-24.04
    steps:
      - uses: example-org/unvetted-action@${UNVETTED_SHA}
EOF
}

add_apt_package() {
  echo "unvetted-package" >>"$1/deploy/toolchain/apt-packages.txt"
}

# expect_rejected SEED PATTERN: a copy with the seed added must fail the check
# with PATTERN in its output.
expect_rejected() {
  local seed="$1" pattern="$2" copy="${WORK}/$1" output
  copy_tree "${copy}"
  "${seed}" "${copy}"
  if output="$(python3 "${CHECK}" check --root "${copy}" 2>&1)"; then
    echo "error: the check accepted a new unregistered dependency (${seed})" >&2
    exit 1
  fi
  if ! grep -q -- "${pattern}" <<<"${output}"; then
    echo "error: the check failed for another reason (${seed}):" >&2
    echo "${output}" >&2
    exit 1
  fi
  echo "Rejected by the register check: ${seed}"
}

main() {
  python3 "${CHECK}" check --root "${ROOT}"
  expect_rejected add_npm_package "unregistered npm dependency 'left-pad'"
  expect_rejected add_github_action "unregistered github-action dependency 'example-org/unvetted-action'"
  expect_rejected add_apt_package "unregistered apt dependency 'unvetted-package'"
  echo "Dependency register: ok; every seeded dependency was rejected"
}

main
