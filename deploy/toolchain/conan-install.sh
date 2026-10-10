#!/usr/bin/env bash
# Install the Conan dependencies that CMake presets need (ICS-004).
#
# Usage: conan-install.sh PRESET...   (for example gcc-release, clang-asan, clang-fuzz or clang-coverage)
#
# Dependencies come from cpp/conan.lock and land in
# cpp/build/conan/<profile>-<build type>[-<sanitizer>]/, where the preset's
# toolchain file points. The asan, tsan and fuzz presets build the dependencies
# with the matching sanitizer, from cpp/conan/profiles/asan or tsan (ICS-011).
#
# Two optional settings serve conan-deps.sh: CONAN_GRAPH_JSON names a file to
# write the install graph to, and CONAN_OFFLINE=1 installs from the Conan cache
# alone, building and downloading nothing.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
readonly ROOT
readonly MAX_PRESETS=16

profile_for() {
  case "${1%%-*}" in
    gcc) echo gcc13 ;;
    clang) echo clang17 ;;
    *) return 1 ;;
  esac
}

build_type_for() {
  case "${1#*-}" in
    release) echo Release ;;
    debug | asan | tsan | ubsan | fuzz | coverage) echo Debug ;;
    *) return 1 ;;
  esac
}

# The sanitizer the dependencies need; empty when they are built plainly.
# UBSan checks only instrumented code and needs nothing from the dependencies,
# and coverage (ICS-015) measures only ICS code.
# libFuzzer runs with ASan, so the fuzz preset shares the ASan dependencies.
sanitizer_for() {
  case "${1#*-}" in
    asan | fuzz) echo asan ;;
    tsan) echo tsan ;;
    *) echo "" ;;
  esac
}

install_preset() {
  local preset="$1"
  local profile build_type sanitizer
  if ! profile="$(profile_for "${preset}")" || ! build_type="$(build_type_for "${preset}")"; then
    echo "unknown preset '${preset}'; see cpp/CMakePresets.json" >&2
    exit 2
  fi
  sanitizer="$(sanitizer_for "${preset}")"
  local fetch=(--build=missing)
  if [[ "${CONAN_OFFLINE:-0}" == 1 ]]; then
    fetch=(--no-remote --build=never)
  fi
  local graph=()
  if [[ -n "${CONAN_GRAPH_JSON:-}" ]]; then
    graph=(--format json --out-file "${CONAN_GRAPH_JSON}")
  fi
  conan install "${ROOT}/cpp" \
    --profile:all "${ROOT}/cpp/conan/profiles/${profile}" \
    ${sanitizer:+--profile:host "${ROOT}/cpp/conan/profiles/${sanitizer}"} \
    --settings:all "build_type=${build_type}" \
    --lockfile "${ROOT}/cpp/conan.lock" \
    --output-folder "${ROOT}/cpp/build/conan/${profile}-${build_type}${sanitizer:+-${sanitizer}}" \
    "${fetch[@]}" "${graph[@]}"
}

main() {
  local preset
  if (($# == 0 || $# > MAX_PRESETS)); then
    echo "usage: conan-install.sh PRESET... (at most ${MAX_PRESETS})" >&2
    exit 2
  fi
  for preset in "$@"; do
    install_preset "${preset}"
  done
}

main "$@"
