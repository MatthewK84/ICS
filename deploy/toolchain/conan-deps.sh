#!/usr/bin/env bash
# Name, package and restore the Conan packages of one dependency
# configuration. CI keeps each configuration's packages in GHCR as a signed
# artifact (deploy/evidence/dependency-artifact.sh) instead of GitHub's
# Actions cache, so a configuration is built once for each key, not again
# whenever a cache expires. Runs in the ics-cpp image.
#
# Usage: conan-deps.sh key
#        conan-deps.sh build CONFIG FILE
#        conan-deps.sh restore FILE
#
# CONFIG names a dependency configuration as conan-install.sh names its output
# folders: gcc13-Release, clang17-Debug-asan and so on.
#
# key prints the key the artifacts are tagged with: a sha256 over everything
# that decides the packages' contents, which is the lockfile, the conanfile,
# the profiles, this script and conan-install.sh, and the versions of the
# compilers, linker, C library, CMake and Conan in the image.
#
# build installs CONFIG's packages, building those that are missing, and saves
# the packages its install graph uses, host packages and build tools both, to
# FILE (a .tgz). It then restores FILE into an empty Conan home and installs
# from it alone, building and downloading nothing, so an archive that lacks a
# package fails here instead of in the jobs that use it.
#
# restore puts FILE's packages into the Conan cache.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
readonly ROOT
readonly INSTALL="${ROOT}/deploy/toolchain/conan-install.sh"

usage() {
  echo "usage: conan-deps.sh key | build CONFIG FILE | restore FILE" >&2
  exit 2
}

# The preset whose dependencies are the configuration's.
preset_for() {
  case "$1" in
    gcc13-Release) echo gcc-release ;;
    clang17-Release) echo clang-release ;;
    gcc13-Debug) echo gcc-debug ;;
    clang17-Debug) echo clang-debug ;;
    gcc13-Debug-asan) echo gcc-asan ;;
    clang17-Debug-asan) echo clang-asan ;;
    gcc13-Debug-tsan) echo gcc-tsan ;;
    clang17-Debug-tsan) echo clang-tsan ;;
    *) return 1 ;;
  esac
}

# sed reads every line, so the tool never writes into a closed pipe.
print_key() {
  {
    (cd "${ROOT}" && sha256sum cpp/conan.lock cpp/conanfile.py cpp/conan/profiles/* \
      deploy/toolchain/conan-install.sh deploy/toolchain/conan-deps.sh)
    gcc-13 --version | sed -n 1p
    clang-17 --version | sed -n 1p
    ld --version | sed -n 1p
    ldd --version | sed -n 1p
    cmake --version | sed -n 1p
    conan --version
  } | sha256sum | cut -d ' ' -f 1
}

# Restores the archive into an empty Conan home and installs from it alone.
check_archive() {
  local preset="$1" file="$2" home
  home="$(mktemp -d)"
  CONAN_HOME="${home}" conan cache restore -vwarning "${file}" >/dev/null
  if ! CONAN_HOME="${home}" CONAN_OFFLINE=1 "${INSTALL}" "${preset}" >"${home}.log" 2>&1; then
    tail -n 40 "${home}.log" >&2
    echo "error: ${file} lacks packages ${preset} needs" >&2
    exit 1
  fi
  rm -rf "${home}" "${home}.log"
}

build_archive() {
  local config="$1" file="$2" preset work
  preset="$(preset_for "${config}")" || usage
  work="$(mktemp -d)"
  CONAN_GRAPH_JSON="${work}/graph.json" "${INSTALL}" "${preset}"
  # Build folders are not saved, and with sanitizers they take gigabytes.
  conan cache clean "*" --source --build --download --temp
  # Every recipe in the graph, with the binaries in the cache: a build tool
  # whose binary the install skipped keeps its recipe, which the graph needs.
  conan list --graph "${work}/graph.json" --format json --out-file "${work}/packages.json"
  mkdir -p "$(dirname "${file}")"
  conan cache save --list "${work}/packages.json" --file "${file}" --no-source >/dev/null
  check_archive "${preset}" "${file}"
  # The check pointed the preset's generated files at its own Conan home.
  "${INSTALL}" "${preset}" >/dev/null
  rm -rf "${work}"
  echo "saved the ${config} packages to ${file}: $(du -h "${file}" | cut -f 1)"
}

main() {
  case "${1:-}" in
    key)
      (($# == 1)) || usage
      print_key
      ;;
    build)
      (($# == 3)) || usage
      build_archive "$2" "$3"
      ;;
    restore)
      (($# == 2)) || usage
      conan cache restore -vwarning "$2" >/dev/null
      ;;
    *) usage ;;
  esac
}

main "$@"
