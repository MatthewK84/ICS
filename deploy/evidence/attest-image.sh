#!/usr/bin/env bash
# Sign a published toolchain image and attach its SBOM and SLSA provenance
# (ICS-009). Runs in GitHub Actions with id-token: write after the image is
# pushed; cosign signs keyless with the workflow's identity through the public
# Sigstore service and stores the signature and attestations in the registry.
#
# Usage: attest-image.sh IMAGE TAG SBOM
#
# IMAGE is the registry name (ghcr.io/matthewk84/ics-cpp), TAG a tag just
# pushed, SBOM the image's CycloneDX SBOM (image-sbom.sh). The image is signed
# by digest, never by tag. The signature and both attestations must then
# verify against this workflow run's identity. Needs cosign on the PATH and a
# registry login.
set -euo pipefail

readonly IMAGE="${1:?usage: attest-image.sh IMAGE TAG SBOM}"
readonly TAG="${2:?usage: attest-image.sh IMAGE TAG SBOM}"
readonly SBOM="${3:?usage: attest-image.sh IMAGE TAG SBOM}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
readonly ROOT
readonly SHA="${GITHUB_SHA:?GITHUB_SHA must be set}"
# A reusable workflow signs with its own identity, not its caller's, which is
# what GITHUB_WORKFLOW_REF names there; it passes its own as SIGNER_WORKFLOW_REF.
readonly IDENTITY="${GITHUB_SERVER_URL:?}/${SIGNER_WORKFLOW_REF:-${GITHUB_WORKFLOW_REF:?}}"
readonly ISSUER="https://token.actions.githubusercontent.com"
WORK="$(mktemp -d)"
readonly WORK
trap 'rm -rf "${WORK}"' EXIT

# The digest the registry holds. The local image's RepoDigests can name a
# digest that was never pushed, so ask the registry.
registry_digest() {
  docker buildx imagetools inspect "${IMAGE}:${TAG}" --format '{{json .Manifest.Digest}}' | tr -d '"'
}

verify_all() {
  local reference="$1"
  local identity=(--certificate-identity "${IDENTITY}" --certificate-oidc-issuer "${ISSUER}"
    --certificate-github-workflow-sha "${SHA}")
  cosign verify "${identity[@]}" "${reference}" >/dev/null
  cosign verify-attestation --type cyclonedx "${identity[@]}" "${reference}" >/dev/null
  cosign verify-attestation --type slsaprovenance1 "${identity[@]}" "${reference}" >/dev/null
  echo "verified the signature, SBOM and provenance of ${reference}"
}

main() {
  local digest reference
  digest="$(registry_digest)"
  if [[ ! "${digest}" =~ ^sha256:[0-9a-f]{64}$ ]]; then
    echo "error: no digest for ${IMAGE}:${TAG}: '${digest}'" >&2
    exit 1
  fi
  reference="${IMAGE}@${digest}"
  python3 "${ROOT}/.github/scripts/evidence.py" predicate --tools "${ROOT}/deploy/evidence/tools.txt" \
    --out "${WORK}/provenance.json"
  cosign sign --yes "${reference}"
  cosign attest --yes --type cyclonedx --predicate "${SBOM}" "${reference}"
  cosign attest --yes --type slsaprovenance1 --predicate "${WORK}/provenance.json" "${reference}"
  verify_all "${reference}"
}

main
