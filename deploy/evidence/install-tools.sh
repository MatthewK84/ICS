#!/usr/bin/env bash
# Install pinned tools into BIN_DIR: the evidence tools in tools.txt beside this
# script (ICS-009) by default, or those in another pins file such as
# proto/tools.txt (ICS-011).
#
# Usage: install-tools.sh BIN_DIR [PINS]
#
# Each download must match its pinned sha256. From a .tar.gz or .zip release
# only the binary named after the tool is kept.
set -euo pipefail

readonly BIN_DIR="${1:?usage: install-tools.sh BIN_DIR [PINS]}"
PINS="${2:-$(cd "$(dirname "$0")" && pwd)/tools.txt}"
readonly PINS
WORK="$(mktemp -d)"
readonly WORK
readonly MAX_TOOLS=16
trap 'rm -rf "${WORK}"' EXIT

# Extract the member of zip archive $1 named $2 (in any folder) to $WORK/$2,
# with Python's zipfile because not every image has unzip.
unzip_binary() {
  python3 - "$1" "$2" "${WORK}/$2" <<'EOF'
import shutil
import sys
import zipfile

archive, name, target = sys.argv[1:]
with zipfile.ZipFile(archive) as bundle:
    members = [m for m in bundle.namelist() if m == name or m.endswith("/" + name)]
    if len(members) != 1:
        sys.exit(f"error: expected one {name} in {archive}, found {members}")
    with bundle.open(members[0]) as source, open(target, "wb") as destination:
        shutil.copyfileobj(source, destination)
EOF
}

# Download URL $1 to $2: with curl, or with Python in an image without curl,
# such as ics-cpp, which CodeQL runs in. The sha256 check follows either way.
fetch_url() {
  if command -v curl >/dev/null; then
    curl --fail --silent --show-error --location --retry 3 --output "$2" "$1"
    return
  fi
  python3 - "$1" "$2" <<'EOF'
import shutil
import sys
import urllib.request

with urllib.request.urlopen(sys.argv[1], timeout=120) as source, open(sys.argv[2], "wb") as target:
    shutil.copyfileobj(source, target)
EOF
}

install_tool() {
  local name="$1" version="$2" sha256="$3" url="$4"
  local download="${WORK}/${url##*/}"
  fetch_url "${url}" "${download}"
  echo "${sha256}  ${download}" | sha256sum --check --quiet
  if [[ "${download}" == *.tar.gz ]]; then
    tar -xzf "${download}" -C "${WORK}" "${name}"
    install -m 0755 "${WORK}/${name}" "${BIN_DIR}/${name}"
  elif [[ "${download}" == *.zip ]]; then
    unzip_binary "${download}" "${name}"
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
