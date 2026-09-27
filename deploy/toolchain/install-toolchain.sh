#!/usr/bin/env bash
# Install the ICS C++ toolchain into an Ubuntu 24.04 image (ICS-004).
#
# Usage: install-toolchain.sh SNAPSHOT_ID
#
# Every Ubuntu package comes from snapshot.ubuntu.com at SNAPSHOT_ID, so a
# rebuild installs the same package versions. Conan is installed into
# /opt/conan from a hash-pinned requirements file. If the build passes a
# BuildKit secret named extra_ca (a PEM bundle), it is trusted for apt, pip and
# the finished image; use it behind TLS-inspecting proxies.
set -euo pipefail

readonly SNAPSHOT_ID="${1:?usage: install-toolchain.sh SNAPSHOT_ID}"
readonly EXTRA_CA_SECRET="/run/secrets/extra_ca"
# apt downloads as the unprivileged _apt user, which cannot read the root-only secret.
readonly EXTRA_CA_COPY="/tmp/ics-extra-ca.pem"
readonly SYSTEM_CA="/etc/ssl/certs/ca-certificates.crt"
readonly HERE="$(cd "$(dirname "$0")" && pwd)"
readonly PACKAGES=(
  ca-certificates
  gcc-13 g++-13
  clang-17 clang-tidy-17 lld-17 llvm-17 libclang-rt-17-dev
  cmake ninja-build
  python3 python3-venv
  git
)
export DEBIAN_FRONTEND=noninteractive

bootstrap_ca_bundle() {
  if [[ -s "${EXTRA_CA_SECRET}" ]]; then
    install -m 0644 "${EXTRA_CA_SECRET}" "${EXTRA_CA_COPY}"
    echo "${EXTRA_CA_COPY}"
    return
  fi
  if [[ -s "${SYSTEM_CA}" ]]; then
    echo "${SYSTEM_CA}"
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

select_default_compilers() {
  update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-13 100
  update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-13 100
  update-alternatives --install /usr/bin/clang clang /usr/bin/clang-17 100
  update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-17 100
}

install_conan() {
  # Conan ships only as source, so its build tool (setuptools) is hash-pinned
  # and installed first, and the build runs without fetching anything else.
  local pip=(/opt/conan/bin/pip install --quiet --no-cache-dir --require-hashes --cert "${SYSTEM_CA}")
  python3 -m venv /opt/conan
  "${pip[@]}" -r "${HERE}/requirements-build.txt"
  "${pip[@]}" --no-build-isolation -r "${HERE}/requirements-conan.txt"
  ln -sf /opt/conan/bin/conan /usr/local/bin/conan
}

report_versions() {
  gcc-13 --version | head -n 1
  clang-17 --version | head -n 1
  clang-tidy-17 --version | grep -m 1 'LLVM version'
  cmake --version | head -n 1
  echo "ninja $(ninja --version)"
  conan --version
  dpkg-query -W -f '${Package}=${Version}\n' "${PACKAGES[@]}" > /usr/local/share/ics-toolchain-packages.txt
}

main() {
  local ca_bundle
  ca_bundle="$(bootstrap_ca_bundle)"
  use_snapshot_sources
  install_packages "${ca_bundle}"
  trust_extra_ca
  select_default_compilers
  install_conan
  report_versions
  rm -f "${EXTRA_CA_COPY}"
}

main
