#!/usr/bin/env bash
# Find the build image a C++ job runs in (#159) in GHCR, and print its
# reference. Main's image comes first; a pull request's own, tagged with
# SUFFIX (-prN), only when main has none for the key. Only the C++ toolchain
# workflow builds images (.github/workflows/conan-deps.yml), so a job in
# another workflow that starts while it is still building one waits for it,
# up to WAIT_MINUTES.
#
# Usage: find-image.sh CONFIG [SUFFIX [WAIT_MINUTES]]
#
# Prints ghcr.io/matthewk84/ics-build:CONFIG-KEY, with SUFFIX if that is the
# one found. Exits 1 when there is none after WAIT_MINUTES (default 0). Needs
# a registry login with read access to the repository's packages.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
readonly HERE
readonly CONFIG="${1:?usage: find-image.sh CONFIG [SUFFIX [WAIT_MINUTES]]}"
readonly SUFFIX="${2:-}"
readonly WAIT_MINUTES="${3:-0}"
readonly REGISTRY="${ICS_BUILD_IMAGE:-ghcr.io/matthewk84/ics-build}"
readonly POLL_S=30

# Prints the first of main's and the pull request's images the registry has.
lookup() {
  local name="$1" ref
  for ref in "${REGISTRY}:${name}" "${REGISTRY}:${name}${SUFFIX}"; do
    if docker buildx imagetools inspect "${ref}" >/dev/null 2>&1; then
      echo "${ref}"
      return 0
    fi
  done
  return 1
}

main() {
  local name deadline ref
  [[ "${WAIT_MINUTES}" =~ ^[0-9]+$ ]] || {
    echo "error: WAIT_MINUTES must be a whole number, not ${WAIT_MINUTES}" >&2
    exit 2
  }
  name="$("${HERE}/image-key.sh" "${CONFIG}")"
  deadline=$((SECONDS + WAIT_MINUTES * 60))
  until ref="$(lookup "${name}")"; do
    if ((SECONDS >= deadline)); then
      echo "error: ${REGISTRY}:${name} is not in the registry; the C++ toolchain workflow builds it" >&2
      exit 1
    fi
    echo "waiting for ${REGISTRY}:${name}, which the C++ toolchain workflow is building" >&2
    sleep "${POLL_S}"
  done
  echo "${ref}"
}

main
