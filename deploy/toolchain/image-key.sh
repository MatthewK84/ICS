#!/usr/bin/env bash
# Name the versioned images the C++ jobs run in (#159): the ics-cpp toolchain
# image (Dockerfile.cpp), and for each dependency configuration a build image,
# the toolchain with that configuration's Conan packages (Dockerfile.build).
# The name is a hash of the committed files that decide the image's contents,
# so a job works it out from its checkout alone, without Docker or the
# network:
#
#   toolchain  Dockerfile.cpp, the files it copies into the image, and this
#              script;
#   CONFIG     the toolchain's key, CONFIG, Dockerfile.build, the Conan
#              lockfile, conanfile and profiles, and the scripts that install
#              the packages.
#
# The compilers, linker and Conan come from the Ubuntu snapshot and the pins
# those files name, so the files decide them too.
#
# Usage: image-key.sh toolchain
#        image-key.sh CONFIG
#
# Prints the toolchain's key, or CONFIG-KEY for a configuration, such as
# gcc13-Release-0123456789abcdef. CONFIG is one of conan-install.sh's output
# folders, as conan-deps.sh names them.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
readonly ROOT
readonly DOCKERFILE="deploy/toolchain/Dockerfile.cpp"
readonly KEY_LENGTH=16

usage() {
  echo "usage: image-key.sh toolchain | CONFIG" >&2
  exit 2
}

# The first KEY_LENGTH hex digits of the SHA-256 of standard input.
digest() {
  sha256sum | cut -c "1-${KEY_LENGTH}"
}

toolchain_key() {
  local copied=()
  # The sources of Dockerfile.cpp's COPY lines; the destination is outside
  # deploy/.
  mapfile -t copied < <(sed -n 's/^COPY //p' "${ROOT}/${DOCKERFILE}" | tr ' ' '\n' | grep '^deploy/')
  if ((${#copied[@]} == 0)); then
    echo "error: ${DOCKERFILE} copies no files from deploy/" >&2
    exit 1
  fi
  (cd "${ROOT}" && sha256sum "${DOCKERFILE}" deploy/toolchain/image-key.sh "${copied[@]}") | digest
}

config_key() {
  local config="$1" toolchain
  toolchain="$(toolchain_key)"
  {
    echo "${toolchain} ${config}"
    (cd "${ROOT}" && sha256sum deploy/toolchain/Dockerfile.build cpp/conan.lock cpp/conanfile.py \
      cpp/conan/profiles/* deploy/toolchain/conan-install.sh deploy/toolchain/conan-deps.sh)
  } | digest
}

main() {
  local key
  (($# == 1)) || usage
  case "$1" in
    toolchain)
      key="$(toolchain_key)"
      echo "${key}"
      ;;
    gcc13-Release | gcc13-Debug | gcc13-Debug-asan | gcc13-Debug-tsan | \
      clang17-Release | clang17-Debug | clang17-Debug-asan | clang17-Debug-tsan)
      key="$(config_key "$1")"
      echo "$1-${key}"
      ;;
    *) usage ;;
  esac
}

main "$@"
