#!/usr/bin/env bash
# Write a CycloneDX SBOM of a local image (ICS-009) and require it to list the
# image's packages of each TYPE: by default its Ubuntu (deb) and Python (pypi)
# packages, as the toolchain images have.
#
# Usage: image-sbom.sh LOCAL_IMAGE NAME OUT_FILE [TYPE...]
#
# NAME is the image's name, recorded in the SBOM. Packages only: the SBOM
# leaves out the per-file listing. Needs syft on the PATH.
set -euo pipefail

readonly LOCAL_IMAGE="${1:?usage: image-sbom.sh LOCAL_IMAGE NAME OUT_FILE [TYPE...]}"
readonly NAME="${2:?usage: image-sbom.sh LOCAL_IMAGE NAME OUT_FILE [TYPE...]}"
readonly OUT_FILE="${3:?usage: image-sbom.sh LOCAL_IMAGE NAME OUT_FILE [TYPE...]}"
shift 3
TYPES=("$@")
if ((${#TYPES[@]} == 0)); then
  TYPES=(deb pypi)
fi
readonly TYPES
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
readonly ROOT
export SYFT_CHECK_FOR_APP_UPDATE=false
export SYFT_FILE_METADATA_SELECTION=none

syft scan "docker:${LOCAL_IMAGE}" --quiet --source-name "${NAME}" --source-version "${GITHUB_SHA:-local}" \
  --output "cyclonedx-json=${OUT_FILE}"
python3 "${ROOT}/.github/scripts/evidence.py" check-sbom "${OUT_FILE}" --require "${TYPES[@]}"
