#!/usr/bin/env bash
# Build the toolchain_check sample twice, in two different build folders, and
# require byte-identical outputs (ICS-004 "Done when").
#
# Usage: check-reproducible.sh gcc|clang [extra cmake arguments]
#
# Set SKIP_CONAN_INSTALL=1 when dependencies are already installed.
set -euo pipefail

readonly COMPILER="${1:?usage: check-reproducible.sh gcc|clang [extra cmake arguments]}"
shift
readonly PRESET="${COMPILER}-release"
readonly ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
readonly WORK="$(mktemp -d)"
readonly OUTPUTS=(
  toolchain_check/libtoolchain_check.a
  toolchain_check/toolchain_check_cli
  toolchain_check/toolchain_check_test
)
export SOURCE_DATE_EPOCH="${SOURCE_DATE_EPOCH:-0}"
trap 'rm -rf "${WORK}"' EXIT

build_in() {
  local dir="$1"
  shift
  (cd "${ROOT}/cpp" && cmake --preset "${PRESET}" -B "${dir}" "$@" >/dev/null)
  cmake --build "${dir}"
  ctest --test-dir "${dir}" --output-on-failure
}

hash_outputs() {
  (cd "$1" && sha256sum "${OUTPUTS[@]}")
}

main() {
  if [[ "${SKIP_CONAN_INSTALL:-0}" != "1" ]]; then
    "${ROOT}/deploy/toolchain/conan-install.sh" "${PRESET}"
  fi
  build_in "${WORK}/first" "$@"
  build_in "${WORK}/second-build-folder" "$@"
  hash_outputs "${WORK}/first" > "${WORK}/first.sha256"
  hash_outputs "${WORK}/second-build-folder" > "${WORK}/second.sha256"
  cat "${WORK}/first.sha256"
  if ! diff -u "${WORK}/first.sha256" "${WORK}/second.sha256"; then
    echo "::error::${PRESET}: outputs differ between the two builds" >&2
    exit 1
  fi
  echo "${PRESET}: ${#OUTPUTS[@]} outputs are byte-identical across two build folders"
}

main "$@"
