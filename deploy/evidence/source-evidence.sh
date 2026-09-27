#!/usr/bin/env bash
# Build, sign and check the evidence for one commit (ICS-009): a source
# archive, a CycloneDX SBOM of its Conan, uv and pnpm lockfiles, and SLSA
# provenance, each signed with cosign as an in-toto attestation about the
# archive. Runs in GitHub Actions with id-token: write; cosign signs keyless
# with the workflow's identity through the public Sigstore service.
#
# Usage: source-evidence.sh OUT_DIR
#
# OUT_DIR receives ics-<sha>.tar, sbom.cdx.json, the two statements and their
# Sigstore bundles (sbom.sigstore.json, provenance.sigstore.json). Each bundle
# must then verify against this workflow run's identity, and a tampered copy of
# the archive must fail verification. Needs syft and cosign on the PATH
# (install-tools.sh).
set -euo pipefail

readonly OUT="${1:?usage: source-evidence.sh OUT_DIR}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
readonly ROOT
readonly EVIDENCE="${ROOT}/.github/scripts/evidence.py"
readonly SHA="${GITHUB_SHA:?GITHUB_SHA must be set}"
readonly IDENTITY="${GITHUB_SERVER_URL:?}/${GITHUB_WORKFLOW_REF:?}"
readonly ISSUER="https://token.actions.githubusercontent.com"
readonly ARCHIVE="ics-${SHA}.tar"
readonly SBOM_TYPE="https://cyclonedx.org/bom"
readonly PROVENANCE_TYPE="https://slsa.dev/provenance/v1"
readonly LOCK_CATALOGERS="conan-cataloger,python-package-cataloger,javascript-lock-cataloger"
WORK="$(mktemp -d)"
readonly WORK
trap 'rm -rf "${WORK}"' EXIT
export SYFT_CHECK_FOR_APP_UPDATE=false

# The SBOM describes the archive itself: Syft scans its unpacked contents.
build_archive_and_sbom() {
  git -C "${ROOT}" archive --format=tar --prefix="ics-${SHA}/" -o "${OUT}/${ARCHIVE}" "${SHA}"
  tar -xf "${OUT}/${ARCHIVE}" -C "${WORK}"
  syft scan "dir:${WORK}/ics-${SHA}" --quiet --override-default-catalogers "${LOCK_CATALOGERS}" \
    --source-name ics --source-version "${SHA}" --output "cyclonedx-json=${OUT}/sbom.cdx.json"
  python3 "${EVIDENCE}" check-sbom "${OUT}/sbom.cdx.json" --require conan pypi npm
}

build_statements() {
  python3 "${EVIDENCE}" predicate --tools "${ROOT}/deploy/evidence/tools.txt" --out "${WORK}/provenance.json"
  python3 "${EVIDENCE}" statement --subject "${OUT}/${ARCHIVE}" --name "${ARCHIVE}" \
    --predicate-type "${SBOM_TYPE}" --predicate "${OUT}/sbom.cdx.json" --out "${OUT}/sbom.statement.json"
  python3 "${EVIDENCE}" statement --subject "${OUT}/${ARCHIVE}" --name "${ARCHIVE}" \
    --predicate-type "${PROVENANCE_TYPE}" --predicate "${WORK}/provenance.json" --out "${OUT}/provenance.statement.json"
}

sign() {
  local name="$1"
  cosign attest-blob --yes --statement "${OUT}/${name}.statement.json" --bundle "${OUT}/${name}.sigstore.json"
}

# verify BUNDLE TYPE FILE: the bundle is a TYPE attestation about FILE, signed
# by this workflow at this commit.
verify() {
  cosign verify-blob-attestation --bundle "$1" --type "$2" \
    --certificate-identity "${IDENTITY}" --certificate-oidc-issuer "${ISSUER}" \
    --certificate-github-workflow-sha "${SHA}" "$3"
}

verify_all() {
  verify "${OUT}/sbom.sigstore.json" cyclonedx "${OUT}/${ARCHIVE}"
  verify "${OUT}/provenance.sigstore.json" slsaprovenance1 "${OUT}/${ARCHIVE}"
  cp "${OUT}/${ARCHIVE}" "${WORK}/tampered.tar"
  printf '\0' >>"${WORK}/tampered.tar"
  if verify "${OUT}/sbom.sigstore.json" cyclonedx "${WORK}/tampered.tar" 2>/dev/null; then
    echo "error: a tampered archive passed verification; the check proves nothing" >&2
    exit 1
  fi
  echo "a tampered archive fails verification, as it must"
}

main() {
  mkdir -p "${OUT}"
  build_archive_and_sbom
  build_statements
  sign sbom
  sign provenance
  verify_all
}

main
