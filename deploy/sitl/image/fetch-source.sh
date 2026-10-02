#!/usr/bin/env bash
# Clone an autopilot source pinned in tools.txt (ICS-018).
#
# Usage: fetch-source.sh NAME DEST [SUBMODULE...]
#
# Clones NAME's release tag into DEST and refuses it unless the tag resolves
# to the pinned commit. Only the listed submodules are fetched, each at the
# commit the release records, so the build cannot pull in anything else.
set -euo pipefail

readonly NAME="${1:?usage: fetch-source.sh NAME DEST [SUBMODULE...]}"
readonly DEST="${2:?usage: fetch-source.sh NAME DEST [SUBMODULE...]}"
shift 2
readonly SUBMODULES=("$@")
HERE="$(cd "$(dirname "$0")" && pwd)"
readonly HERE

read -r TAG COMMIT URL < <(awk -v name="${NAME}" '$1 == name { print $2, $3, $4 }' "${HERE}/tools.txt") || true
if [[ -z "${URL:-}" ]]; then
  echo "error: ${NAME} is not pinned in ${HERE}/tools.txt" >&2
  exit 1
fi
readonly TAG COMMIT URL

git -c advice.detachedHead=false clone --quiet --depth 1 --branch "${TAG}" "${URL}" "${DEST}"
ACTUAL="$(git -C "${DEST}" rev-parse HEAD)"
readonly ACTUAL
if [[ "${ACTUAL}" != "${COMMIT}" ]]; then
  echo "error: ${NAME} ${TAG} is ${ACTUAL}, but tools.txt pins ${COMMIT}" >&2
  exit 1
fi
if ((${#SUBMODULES[@]} > 0)); then
  git -C "${DEST}" submodule --quiet update --init --recursive --depth 1 --jobs 8 -- "${SUBMODULES[@]}"
fi
echo "${NAME} ${TAG} ${ACTUAL}"
