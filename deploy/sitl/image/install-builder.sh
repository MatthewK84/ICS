#!/usr/bin/env bash
# Install what the SITL rig's build stages need to compile an autopilot (ICS-018).
#
# Usage: install-builder.sh SNAPSHOT_ID REQUIREMENTS
#
# Every Ubuntu package in apt-packages.txt comes from snapshot.ubuntu.com at
# SNAPSHOT_ID, as in the ICS toolchain images (deploy/toolchain), and the
# autopilot's Python build tools come from the hash-pinned REQUIREMENTS file,
# installed into /opt/sitl-python. If the build passes a BuildKit secret named
# extra_ca (a PEM bundle), it is trusted for apt, pip and git; use it behind
# TLS-inspecting proxies. Only the build stage runs this: the published images
# copy out the compiled autopilot and nothing else.
set -euo pipefail

readonly SNAPSHOT_ID="${1:?usage: install-builder.sh SNAPSHOT_ID REQUIREMENTS}"
readonly REQUIREMENTS="${2:?usage: install-builder.sh SNAPSHOT_ID REQUIREMENTS}"
readonly EXTRA_CA_SECRET="/run/secrets/extra_ca"
# apt downloads as the unprivileged _apt user, which cannot read the root-only secret.
readonly EXTRA_CA_COPY="/tmp/ics-extra-ca.pem"
readonly SYSTEM_CA="/etc/ssl/certs/ca-certificates.crt"
readonly VENV="/opt/sitl-python"
HERE="$(cd "$(dirname "$0")" && pwd)"
readonly HERE
mapfile -t PACKAGES < <(sed -e 's/#.*//' -e '/^[[:space:]]*$/d' "${HERE}/apt-packages.txt")
readonly PACKAGES
if ((${#PACKAGES[@]} == 0)); then
  echo "error: no packages listed in ${HERE}/apt-packages.txt" >&2
  exit 1
fi
export DEBIAN_FRONTEND=noninteractive

bootstrap_ca_bundle() {
  if [[ -s "${EXTRA_CA_SECRET}" ]]; then
    install -m 0644 "${EXTRA_CA_SECRET}" "${EXTRA_CA_COPY}"
    echo "${EXTRA_CA_COPY}"
    return
  fi
  # No extra CA: install the public CA bundle from the image's default archive
  # so apt can reach the HTTPS snapshot. Pinned so a changed package fails loudly.
  apt-get update -qq >&2
  apt-get install -y -qq --no-install-recommends ca-certificates=20240203 >&2
  echo "${SYSTEM_CA}"
}

use_snapshot_sources() {
  local uri="https://snapshot.ubuntu.com/ubuntu/${SNAPSHOT_ID}/"
  rm -f /etc/apt/sources.list.d/*.list /etc/apt/sources.list.d/*.sources /etc/apt/sources.list
  cat > /etc/apt/sources.list.d/ubuntu.sources <<EOF
Types: deb
URIs: ${uri}
Suites: noble noble-updates noble-security
Components: main universe
Signed-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg
EOF
}

install_packages() {
  local ca_bundle="$1"
  apt-get -o "Acquire::https::CAInfo=${ca_bundle}" update -qq
  apt-get -o "Acquire::https::CAInfo=${ca_bundle}" install -y -qq --no-install-recommends "${PACKAGES[@]}"
  rm -rf /var/lib/apt/lists/*
}

trust_extra_ca() {
  if [[ -s "${EXTRA_CA_SECRET}" ]]; then
    cp "${EXTRA_CA_SECRET}" /usr/local/share/ca-certificates/ics-extra-ca.crt
    update-ca-certificates >/dev/null
  fi
}

install_python_tools() {
  # empy ships only as source, so its build tool (setuptools) is hash-pinned
  # and installed first, and the rest installs without fetching anything else.
  local pip=("${VENV}/bin/pip" install --quiet --no-cache-dir --require-hashes --cert "${SYSTEM_CA}")
  python3 -m venv "${VENV}"
  "${pip[@]}" -r "${HERE}/requirements-build.txt"
  "${pip[@]}" --no-build-isolation -r "${REQUIREMENTS}"
}

main() {
  local ca_bundle
  ca_bundle="$(bootstrap_ca_bundle)"
  use_snapshot_sources
  install_packages "${ca_bundle}"
  trust_extra_ca
  install_python_tools
  rm -f "${EXTRA_CA_COPY}"
}

main
