#!/usr/bin/env bash
# The capture bench (ICS-020 "Done when": a 24 h capture with zero drops).
#
# Usage: run-bench.sh ICS_CAPD ICS_CAPTURE_SEND ICS_CAPTURE_VERIFY PACKETS RATE ROTATE_S
#
# Two network namespaces joined by a veth pair. In one, ICS_CAPTURE_SEND sends
# PACKETS numbered UDP datagrams at RATE a second; in the other, ICS_CAPD
# captures the veth end, standing in for a TAP port, with host time stamps,
# rotating its file every ROTATE_S seconds. That namespace has no address and
# no IPv6, and the sender's neighbour entry is static, so the datagrams are
# the only traffic on the link.
#
# As each file closes, the bench checks it with sha256sum -c against its
# .sha256 file, then with ICS_CAPTURE_VERIFY, which requires the datagrams in
# order with none missing or repeated, and deletes it, so a long run needs
# only a few files' worth of disk. Once the sender is done, ics-capd is
# stopped with SIGTERM, which closes and hashes its last file. The bench
# passes when every file's hash matched, the files held exactly the PACKETS
# datagrams sent, ics-capd logged no drops and no failure, and it exited 0.
#
# ics-capd reads its config from /etc/ics/ics-capd.toml. The bench runs it in
# its own mount namespace with the bench's config mounted there, so the host's
# /etc/ics is never changed. An empty /etc/ics is made for the mount if there
# is none, and removed afterwards.
#
# Run as root: network and mount namespaces need it. Needs ip
# (apt-packages.txt), unshare, mount and sha256sum. Writes files.csv (file,
# packets, sha256) and the ics-capd log to $BENCH_OUT, if set.
set -euo pipefail

readonly USAGE="usage: run-bench.sh ICS_CAPD ICS_CAPTURE_SEND ICS_CAPTURE_VERIFY PACKETS RATE ROTATE_S"
readonly CAPD="${1:?${USAGE}}"
readonly SEND="${2:?${USAGE}}"
readonly VERIFY="${3:?${USAGE}}"
readonly PACKETS="${4:?${USAGE}}"
readonly RATE="${5:?${USAGE}}"
readonly ROTATE_S="${6:?${USAGE}}"
readonly TX_NS="ics-cb-tx"
readonly CAP_NS="ics-cb-cap"
readonly TX_IF="ics-cb-tx0"
readonly CAP_IF="ics-cb-cap0"
readonly TX_ADDRESS="10.77.0.1"
readonly CAP_ADDRESS="10.77.0.2"
# The discard port: nothing listens, and nothing in the capture namespace
# would answer anyway.
readonly PORT=9
readonly SETTLE_S=30
WORK="$(mktemp -d /tmp/ics-cb.XXXXXX)"
readonly WORK
readonly FOLDER="${WORK}/capture"
readonly LOG="${WORK}/ics-capd.log"
readonly CSV="${WORK}/files.csv"
readonly ETC_ICS="/etc/ics"
CAPD_PID=""
SEND_PID=""
MADE_ETC_ICS=0
NEXT=0
FILES=0

fail() {
  echo "::error::$*" >&2
  exit 1
}

cleanup() {
  local pid
  for pid in "${SEND_PID}" "${CAPD_PID}"; do
    [[ -z "${pid}" ]] || kill "${pid}" 2>/dev/null || true
  done
  wait 2>/dev/null || true
  ip netns del "${TX_NS}" 2>/dev/null || true
  ip netns del "${CAP_NS}" 2>/dev/null || true
  if ((MADE_ETC_ICS == 1)); then
    rmdir "${ETC_ICS}" 2>/dev/null || true
  fi
  if [[ -n "${BENCH_OUT:-}" ]]; then
    mkdir -p "${BENCH_OUT}"
    cp "${LOG}" "${CSV}" "${WORK}/sent.txt" "${BENCH_OUT}/" 2>/dev/null || true
  fi
  rm -rf "${WORK}"
}
trap cleanup EXIT

# The sender and the capture, each in its own namespace with IPv6 off, on a
# veth pair made after IPv6 is off, so the link never carries IPv6.
make_link() {
  local namespace cap_mac
  for namespace in "${TX_NS}" "${CAP_NS}"; do
    ip netns add "${namespace}"
    # A kernel built without IPv6 has nothing to turn off.
    if [[ -d /proc/sys/net/ipv6 ]]; then
      ip netns exec "${namespace}" sysctl -qw net.ipv6.conf.all.disable_ipv6=1 net.ipv6.conf.default.disable_ipv6=1
    fi
  done
  ip link add "${TX_IF}" netns "${TX_NS}" type veth peer name "${CAP_IF}" netns "${CAP_NS}"
  ip -n "${TX_NS}" link set "${TX_IF}" up
  ip -n "${CAP_NS}" link set "${CAP_IF}" up
  ip -n "${TX_NS}" address add "${TX_ADDRESS}/30" dev "${TX_IF}"
  cap_mac="$(ip -n "${CAP_NS}" -o link show "${CAP_IF}" | sed -n 's/.*link\/ether \([0-9a-f:]*\).*/\1/p')"
  ip -n "${TX_NS}" neighbour replace "${CAP_ADDRESS}" lladdr "${cap_mac}" dev "${TX_IF}" nud permanent
}

# Starts ics-capd in the capture namespace and its own mount namespace, with
# $WORK/etc-ics mounted over /etc/ics there.
start_capd() {
  mkdir -p "${WORK}/etc-ics" "${FOLDER}"
  cat >"${WORK}/etc-ics/ics-capd.toml" <<TOML
[log]
level = "info"
service = "ics-capd"

[capture]
interfaces = ["${CAP_IF}"]
folder = "${FOLDER}"
snaplen = 64
buffer_bytes = 67_108_864
timestamps = "host"
rotate_interval_ns = ${ROTATE_S}_000_000_000
rotate_bytes = 1_099_511_627_776
TOML
  if [[ ! -d "${ETC_ICS}" ]]; then
    mkdir "${ETC_ICS}"
    MADE_ETC_ICS=1
  fi
  ip netns exec "${CAP_NS}" unshare --mount --propagation private sh -c \
    "mount --bind \"\$1\" \"\$2\" && exec \"\$3\"" sh "${WORK}/etc-ics" "${ETC_ICS}" "${CAPD}" 2>"${LOG}" &
  CAPD_PID="$!"
}

# wait_until SECONDS WHAT COMMAND...: runs COMMAND every 50 ms until it
# succeeds; fails naming WHAT after SECONDS.
wait_until() {
  local seconds="$1" what="$2" deadline
  shift 2
  deadline=$((SECONDS + seconds))
  until "$@"; do
    ((SECONDS < deadline)) || fail "timed out after ${seconds} s waiting for ${what}"
    sleep 0.05
  done
}

capd_started() {
  grep -q '"event":"started"' "${LOG}"
}

# Checks, records and deletes every file ics-capd has closed, oldest first.
check_closed_files() {
  local sidecar file name packets next
  for sidecar in $(find "${FOLDER}" -name '*.pcap.sha256' | sort); do
    file="${sidecar%.sha256}"
    name="$(basename "${file}")"
    (cd "${FOLDER}" && sha256sum --check --quiet "${name}.sha256") || fail "${name} does not match its hash"
    next="$("${VERIFY}" "${file}" "${NEXT}")" || fail "${name} is missing packets or holds them out of order"
    packets=$((next - NEXT))
    echo "${name},${packets},$(cut -d' ' -f1 "${sidecar}")" >>"${CSV}"
    echo "Checked ${name}: ${packets} packets, hash matched"
    NEXT="${next}"
    FILES=$((FILES + 1))
    rm -f "${file}" "${sidecar}"
  done
}

# Fails unless ics-capd logged no drops and no failure, and exited 0.
judge() {
  local status=0 sent
  wait "${CAPD_PID}" || status=$?
  CAPD_PID=""
  check_closed_files
  sent="$(cat "${WORK}/sent.txt")"
  ((status == 0)) || fail "ics-capd exited ${status}; see its log"
  ! grep -qE '"event":"(drops|capture_failed|close_failed|counters_unavailable)"' "${LOG}" \
    || fail "ics-capd logged drops or a failure: $(grep -E '"event":"(drops|capture_failed|close_failed)"' "${LOG}" | head -n 3)"
  ((sent == PACKETS)) || fail "the sender sent ${sent} of ${PACKETS} datagrams"
  ((NEXT == PACKETS)) || fail "the files held ${NEXT} of the ${PACKETS} datagrams sent"
  echo "Capture bench: ok; ${PACKETS} datagrams at ${RATE}/s in ${FILES} files, every hash matched, zero drops"
}

main() {
  ((EUID == 0)) || fail "run as root: network namespaces need it"
  local tool
  for tool in "${CAPD}" "${SEND}" "${VERIFY}"; do
    [[ -x "${tool}" ]] || fail "${tool} is not an executable"
  done
  [[ "${PACKETS}" =~ ^[1-9][0-9]*$ && "${RATE}" =~ ^[1-9][0-9]*$ && "${ROTATE_S}" =~ ^[1-9][0-9]*$ ]] \
    || fail "PACKETS, RATE and ROTATE_S must be positive integers"
  make_link
  start_capd
  wait_until "${SETTLE_S}" "ics-capd to start" capd_started
  ip netns exec "${TX_NS}" "${SEND}" "${CAP_ADDRESS}" "${PORT}" "${PACKETS}" "${RATE}" >"${WORK}/sent.txt" &
  SEND_PID="$!"
  while kill -0 "${SEND_PID}" 2>/dev/null; do
    check_closed_files
    sleep 1
  done
  wait "${SEND_PID}" || fail "the sender failed after $(cat "${WORK}/sent.txt") datagrams"
  SEND_PID=""
  # Give the last datagrams time to reach the file before stopping.
  sleep 1
  kill -TERM "${CAPD_PID}"
  judge
}

main
