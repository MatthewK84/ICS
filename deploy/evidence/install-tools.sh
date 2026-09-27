#!/usr/bin/env bash
# Install the evidence tools pinned in tools.txt (ICS-009) into BIN_DIR.
#
# Usage: install-tools.sh BIN_DIR
#
# Each download must match its pinned sha256. A .tar.gz release is unpacked
# and only the binary named after the tool is kept.
set -euo pipefail

readonly BIN_DIR="${1:?usage: install-tools.sh BIN_DIR}"
PINS="$(cd "$(dirname "$0")" && pwd)/tools.txt"
readonly PINS
WORK="$(mktemp -d)"
readonly WORK
readonly MAX_TOOLS=16
trap 'rm -rf "${WORK}"' EXIT

install_tool() {
  local name="$1" version="$2" sha256="$3" url="$4"
  local download="${WORK}/${url##*/}"
  curl --fail --silent --show-error --location --retry 3 --output "${download}" "${url}"
  echo "${sha256}  ${download}" | sha256sum --check --quiet
  if [[ "${download}" == *.tar.gz ]]; then
    tar -xzf "${download}" -C "${WORK}" "${name}"
    install -m 0755 "${WORK}/${name}" "${BIN_DIR}/${name}"
  else
    install -m 0755 "${download}" "${BIN_DIR}/${name}"
  fi
  echo "installed ${name} ${version}"
}

main() {
  local count=0 name version sha256 url
  mkdir -p "${BIN_DIR}"
  while read -r name version sha256 url; do
    if [[ -z "${name}" || "${name}" == \#* ]]; then
      continue
    fi
    count=$((count + 1))
    if ((count > MAX_TOOLS)); then
      echo "error: more than ${MAX_TOOLS} tools in ${PINS}" >&2
      exit 1
    fi
    install_tool "${name}" "${version}" "${sha256}" "${url}"
  done <"${PINS}"
}

main
