#!/usr/bin/env bash
# Fetch and publish the signed artifacts that hold the Conan packages of each
# dependency configuration (deploy/toolchain/conan-deps.sh). Runs on a GitHub
# Actions runner, logged in to ghcr.io, with oras and cosign on the PATH; to
# publish, the job also needs packages: write and id-token: write.
#
# Usage: dependency-artifact.sh fetch CONFIG KEY FOLDER [for-build]
#        dependency-artifact.sh publish CONFIG KEY FILE
#
# The artifacts are tags of ghcr.io/<owner>/ics-conan:
#   CONFIG-KEY        built on main for KEY (conan-deps.sh key)
#   CONFIG-KEY-prN    built for pull request N when main had none for KEY
#   CONFIG-main       the last one built on main, whatever its key
# The dependency workflow (.github/workflows/conan-deps.yml) signs each one
# keyless, and an artifact counts only with that workflow's signature from
# main, or, in pull request N, from pull request N. Main never uses packages
# that a pull request's code built.
#
# fetch looks for KEY's artifact (main's, then on a pull request its own) and
# then for CONFIG-main. It verifies the first it finds and pulls it into
# FOLDER/conan-packages.tgz, except that for-build skips pulling KEY's own:
# the dependency workflow then has nothing to build. An artifact that is there
# but does not verify fails the fetch. It prints the step outputs
# match=current|older|none and pulled=true|false.
#
# publish pushes FILE (named conan-packages.tgz) as KEY's artifact for this
# ref, signs it by digest and verifies the signature. On main it then points
# CONFIG-main at it.
set -euo pipefail

readonly ISSUER="https://token.actions.githubusercontent.com"
readonly ARTIFACT_TYPE="application/vnd.ics.conan-packages.v1"
readonly MEDIA_TYPE="application/vnd.ics.conan-packages.v1.tar+gzip"
readonly ARCHIVE="conan-packages.tgz"
readonly MAIN_REF="refs/heads/main"
REPOSITORY="ghcr.io/${GITHUB_REPOSITORY_OWNER:?}/ics-conan"
readonly REPOSITORY="${REPOSITORY,,}"
readonly WORKFLOW="${GITHUB_SERVER_URL:?}/${GITHUB_REPOSITORY:?}/.github/workflows/conan-deps.yml"
readonly REF="${GITHUB_REF:?}"

usage() {
  echo "usage: dependency-artifact.sh fetch CONFIG KEY FOLDER [for-build] | publish CONFIG KEY FILE" >&2
  exit 2
}

# The pull request's number when this run is for one, or nothing.
pull_request() {
  if [[ "${REF}" =~ ^refs/pull/([0-9]+)/merge$ ]]; then
    echo "${BASH_REMATCH[1]}"
  fi
}

# Prints the digest TAG names. Returns 1 when the tag, or the whole package,
# is not there (GHCR denies a package that does not exist yet, or calls its
# name unknown), and 2 for any other error.
digest_of() {
  local out
  if out="$(oras resolve "${REPOSITORY}:$1" 2>&1)" && [[ "${out}" =~ ^sha256:[0-9a-f]{64}$ ]]; then
    echo "${out}"
    return 0
  fi
  if [[ "${out}" == *"not found"* || "${out}" == *"denied"* || "${out}" == *"name unknown"* ]]; then
    return 1
  fi
  echo "error: cannot look up ${REPOSITORY}:$1: ${out}" >&2
  return 2
}

# Verifies that the dependency workflow signed REFERENCE on REF_NAME, or ends
# the run with cosign's reason, which names the identity that did sign it.
verify() {
  local reference="$1" ref_name="$2" out
  if out="$(cosign verify --certificate-identity "${WORKFLOW}@${ref_name}" --certificate-oidc-issuer "${ISSUER}" \
    "${reference}" 2>&1 >/dev/null)"; then
    return 0
  fi
  echo "::error::${reference} is not signed by ${WORKFLOW}@${ref_name}: $(echo "${out}" | tail -n 1)" >&2
  exit 1
}

# "match tag ref" lines: the tags to look for, in order, each with the ref
# whose dependency workflow must have signed it.
candidates() {
  local config="$1" key="$2" number
  number="$(pull_request)"
  echo "current ${config}-${key} ${MAIN_REF}"
  if [[ -n "${number}" ]]; then
    echo "current ${config}-${key}-pr${number} ${REF}"
  fi
  echo "older ${config}-main ${MAIN_REF}"
}

fetch() {
  local config="$1" key="$2" folder="$3" for_build="$4" match tag ref_name digest status
  while read -r match tag ref_name; do
    status=0
    digest="$(digest_of "${tag}")" || status=$?
    if ((status == 1)); then
      continue
    fi
    ((status == 0)) || exit 1
    verify "${REPOSITORY}@${digest}" "${ref_name}"
    echo "using ${REPOSITORY}:${tag} (${digest})" >&2
    echo "match=${match}"
    if [[ "${match}" == current && "${for_build}" == for-build ]]; then
      echo "pulled=false"
      return 0
    fi
    mkdir -p "${folder}"
    rm -f "${folder}/${ARCHIVE}"
    oras pull --output "${folder}" "${REPOSITORY}@${digest}" >&2
    [[ -f "${folder}/${ARCHIVE}" ]] || { echo "error: ${tag} holds no ${ARCHIVE}" >&2 && exit 1; }
    echo "pulled=true"
    return 0
  done < <(candidates "${config}" "${key}")
  echo "::warning::no ${config} dependency artifact for key ${key} or from main; Conan builds every package" >&2
  echo "match=none"
  echo "pulled=false"
}

# The tag this run publishes KEY's artifact under.
own_tag() {
  local config="$1" key="$2" number
  number="$(pull_request)"
  if [[ "${REF}" == "${MAIN_REF}" ]]; then
    echo "${config}-${key}"
  elif [[ -n "${number}" ]]; then
    echo "${config}-${key}-pr${number}"
  else
    echo "error: artifacts are published from main or a pull request, not ${REF}" >&2
    exit 1
  fi
}

publish() {
  local config="$1" key="$2" file="$3" tag manifest digest
  tag="$(own_tag "${config}" "${key}")"
  [[ "$(basename "${file}")" == "${ARCHIVE}" ]] || usage
  manifest="$(mktemp)"
  (cd "$(dirname "${file}")" && oras push "${REPOSITORY}:${tag}" --artifact-type "${ARTIFACT_TYPE}" \
    --annotation "org.opencontainers.image.source=${GITHUB_SERVER_URL}/${GITHUB_REPOSITORY}" \
    --export-manifest "${manifest}" "${ARCHIVE}:${MEDIA_TYPE}")
  digest="sha256:$(sha256sum "${manifest}" | cut -d ' ' -f 1)"
  rm -f "${manifest}"
  cosign sign --yes "${REPOSITORY}@${digest}"
  verify "${REPOSITORY}@${digest}" "${REF}"
  if [[ "${REF}" == "${MAIN_REF}" ]]; then
    oras tag "${REPOSITORY}@${digest}" "${config}-main"
  fi
  echo "published ${REPOSITORY}:${tag} (${digest})"
}

main() {
  case "${1:-}" in
    fetch)
      (($# == 4 || $# == 5)) || usage
      fetch "$2" "$3" "$4" "${5:-}"
      ;;
    publish)
      (($# == 4)) || usage
      publish "$2" "$3" "$4"
      ;;
    *) usage ;;
  esac
}

main "$@"
