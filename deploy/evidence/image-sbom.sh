#!/usr/bin/env bash
# Write a CycloneDX SBOM of a local toolchain image (ICS-009) and require it
# to list the image's Ubuntu (deb) and Python (pypi) packages.
#
# Usage: image-sbom.sh LOCAL_IMAGE NAME OUT_FILE
#
# NAME is the image's published name, recorded in the SBOM. Packages only:
# the SBOM leaves out the per-file listing. Needs syft on the PATH.
set -euo pipefail

readonly LOCAL_IMAGE="${1:?usage: image-sbom.sh LOCAL_IMAGE NAME OUT_FILE}"
readonly NAME="${2:?usage: image-sbom.sh LOCAL_IMAGE NAME OUT_FILE}"
readonly OUT_FILE="${3:?usage: image-sbom.sh LOCAL_IMAGE NAME OUT_FILE}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
readonly ROOT
export SYFT_CHECK_FOR_APP_UPDATE=false
export SYFT_FILE_METADATA_SELECTION=none

syft scan "docker:${LOCAL_IMAGE}" --quiet --source-name "${NAME}" --source-version "${GITHUB_SHA:-local}" \
  --output "cyclonedx-json=${OUT_FILE}"
python3 "${ROOT}/.github/scripts/evidence.py" check-sbom "${OUT_FILE}" --require deb pypi
