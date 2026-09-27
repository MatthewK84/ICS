#!/usr/bin/env bash
# Install the Conan dependencies that a CMake preset needs (ICS-004).
#
# Usage: conan-install.sh PRESET   (for example gcc-release, clang-asan or clang-fuzz)
#
# Dependencies come from cpp/conan.lock and land in
# cpp/build/conan/<profile>-<build type>/, where the preset's toolchain file
# points.
set -euo pipefail

readonly PRESET="${1:?usage: conan-install.sh PRESET}"
readonly ROOT="$(cd "$(dirname "$0")/../.." && pwd)"

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
    debug | asan | tsan | ubsan | fuzz) echo Debug ;;
    *) return 1 ;;
  esac
}

main() {
  local profile build_type
  if ! profile="$(profile_for "${PRESET}")" || ! build_type="$(build_type_for "${PRESET}")"; then
    echo "unknown preset '${PRESET}'; see cpp/CMakePresets.json" >&2
    exit 2
  fi
  conan install "${ROOT}/cpp" \
    --profile:all "${ROOT}/cpp/conan/profiles/${profile}" \
    --settings:all "build_type=${build_type}" \
    --lockfile "${ROOT}/cpp/conan.lock" \
    --output-folder "${ROOT}/cpp/build/conan/${profile}-${build_type}" \
    --build=missing
}

main
