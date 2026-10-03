#!/usr/bin/env bash
# The capture bench (ICS-020 "Done when": a 24 h capture with zero drops).
#
# Usage: run-bench.sh ICS_CAPD ICS_LOADGEN PACKETS RATE ROTATE_SECONDS
#
# Two network namespaces joined by a veth pair: ics-loadgen sends PACKETS UDP
# datagrams at RATE a second from one, and ICS_CAPD captures the other end,
# as it would a TAP port. The link carries nothing else: IPv6 is off and the
# neighbour is static, so no ARP. Files rotate every ROTATE_SECONDS.
#
# While the load runs, each file ics-capd closes is checked against its
# .sha256 and deleted, so the disk holds only a few at a time. The bench
# passes when every file's hash matches, ics-capd counted no drops, and the
# files held exactly the datagrams sent.
#
# The accelerated soak sends what 24 h at 2,000 packets a second would
# (172,800,000) at 100,000 a second, rotating every 72 s for the 24 hourly
# files a day makes. Hourly rotation is what the example config sets.
#
# ics-capd reads /etc/ics/ics-capd.toml, so it runs in its own mount namespace
# with the bench's config mounted there; the host's /etc/ics is never
# changed. Run as root, with ip, unshare, mount and sha256sum. Prints a
# summary, and writes it with files.csv and the ics-capd log to $BENCH_OUT,
# if set.
set -euo pipefail

readonly CAPD="${1:?usage: run-bench.sh ICS_CAPD ICS_LOADGEN PACKETS RATE ROTATE_SECONDS}"
readonly LOADGEN="${2:?usage: run-bench.sh ICS_CAPD ICS_LOADGEN PACKETS RATE ROTATE_SECONDS}"
readonly PACKETS="${3:?usage: run-bench.sh ICS_CAPD ICS_LOADGEN PACKETS RATE ROTATE_SECONDS}"
readonly RATE="${4:?usage: run-bench.sh ICS_CAPD ICS_LOADGEN PACKETS RATE ROTATE_SECONDS}"
readonly ROTATE_S="${5:?usage: run-bench.sh ICS_CAPD ICS_LOADGEN PACKETS RATE ROTATE_SECONDS}"
readonly CAP_NS="ics-cb-cap"
readonly GEN_NS="ics-cb-gen"
readonly CAP_IF="ics-cb-cap0"
readonly GEN_IF="ics-cb-gen0"
readonly GEN_IP="10.200.20.1"
readonly TARGET_IP="10.200.20.2"
readonly SNAPLEN=64
readonly PAYLOAD=64
readonly SETTLE_S=30
readonly ETC_ICS="/etc/ics"
WORK="$(mktemp -d /tmp/ics-cb.XXXXXX)"
readonly WORK
readonly LOG="${WORK}/ics-capd.log"
readonly OUT="${WORK}/out"
readonly CSV="${WORK}/files.csv"
readonly SUMMARY="${WORK}/summary.txt"
PIDS=()
MADE_ETC_ICS=0
CAPD_PID=""
VERIFIED=0
CAPTURED=0
DROPS=0

fail() {
  echo "::error::$*" >&2
  exit 1
}

cleanup() {
  local pid
  for pid in "${PIDS[@]}"; do
    kill "${pid}" 2>/dev/null || true
  done
  wait 2>/dev/null || true
  ip netns del "${CAP_NS}" 2>/dev/null || true
  ip netns del "${GEN_NS}" 2>/dev/null || true
  if ((MADE_ETC_ICS == 1)); then
    rmdir "${ETC_ICS}" 2>/dev/null || true
  fi
  if [[ -n "${BENCH_OUT:-}" ]]; then
    mkdir -p "${BENCH_OUT}"
    cp "${LOG}" "${CSV}" "${SUMMARY}" "${WORK}/loadgen.out" "${BENCH_OUT}/" 2>/dev/null || true
  fi
  rm -rf "${WORK}"
}
trap cleanup EXIT

# Two namespaces on a veth pair that carries only the generator's datagrams.
make_link() {
  local ns mac
  for ns in "${CAP_NS}" "${GEN_NS}"; do
    ip netns add "${ns}"
    # A kernel without IPv6 sends none.
    if [[ -d /proc/sys/net/ipv6 ]]; then
      ip netns exec "${ns}" sysctl -qw net.ipv6.conf.all.disable_ipv6=1 net.ipv6.conf.default.disable_ipv6=1
    fi
  done
  ip link add "${GEN_IF}" netns "${GEN_NS}" type veth peer name "${CAP_IF}" netns "${CAP_NS}"
  ip -n "${CAP_NS}" link set "${CAP_IF}" up
  ip -n "${GEN_NS}" link set "${GEN_IF}" up
  ip -n "${GEN_NS}" addr add "${GEN_IP}/24" dev "${GEN_IF}"
  mac="$(ip -n "${CAP_NS}" -br link show "${CAP_IF}" | awk '{print $3}')"
  ip -n "${GEN_NS}" neigh replace "${TARGET_IP}" lladdr "${mac}" dev "${GEN_IF}" nud permanent
}

# Starts ics-capd on the capture end, in its own mount namespace with the
# bench's config mounted over /etc/ics.
start_capd() {
  mkdir -p "${WORK}/etc-ics" "${OUT}"
  cat >"${WORK}/etc-ics/ics-capd.toml" <<EOF
[log]
level = "info"
service = "ics-capd"

[capture]
interfaces = ["${CAP_IF}"]
output_dir = "${OUT}"
snaplen = ${SNAPLEN}
ring_bytes = 67_108_864
timestamps = "host"
rotate_interval_ns = ${ROTATE_S}_000_000_000
rotate_bytes = 1_099_511_627_776
ptp4l_socket = "${WORK}/ptp4l-ro"
client_socket = "${WORK}/capd-client"
ptp_domain = 0
EOF
  if [[ ! -d "${ETC_ICS}" ]]; then
    mkdir "${ETC_ICS}"
    MADE_ETC_ICS=1
  fi
  ip netns exec "${CAP_NS}" unshare --mount --propagation private \
    sh -c "mount --bind \"\$1\" \"\$2\" && exec \"\$3\"" sh "${WORK}/etc-ics" "${ETC_ICS}" "${CAPD}" 2>"${LOG}" &
  CAPD_PID=$!
  PIDS+=("${CAPD_PID}")
}

# wait_until SECONDS WHAT COMMAND...
wait_until() {
  local seconds="$1" what="$2" deadline
  shift 2
  deadline=$((SECONDS + seconds))
  until "$@"; do
    ((SECONDS < deadline)) || fail "timed out after ${seconds} s waiting for ${what}"
    sleep 0.1
  done
}

capd_started() {
  grep -q '"event":"started"' "${LOG}"
}

# The number after "KEY": in a log line.
number_field() {
  sed -n "s/.*\"$2\":\\([0-9]*\\).*/\\1/p" <<<"$1"
}

# Checks each file ics-capd closed since the last call against its .sha256,
# adds up its packets and drops, and deletes it.
verify_closed() {
  local lines line path packets dropped interface_dropped
  mapfile -t lines < <(grep '"event":"file_closed"' "${LOG}" | tail -n +"$((VERIFIED + 1))")
  for line in "${lines[@]}"; do
    path="$(sed -n 's/.*"path":"\([^"]*\)".*/\1/p' <<<"${line}")"
    packets="$(number_field "${line}" packets)"
    dropped="$(number_field "${line}" dropped)"
    interface_dropped="$(number_field "${line}" interface_dropped)"
    (cd "${OUT}" && sha256sum --quiet --strict -c "${path##*/}.sha256") || fail "${path##*/} does not match its .sha256"
    echo "${path##*/},${packets},${dropped},${interface_dropped}" >>"${CSV}"
    CAPTURED=$((CAPTURED + packets))
    DROPS=$((DROPS + dropped + interface_dropped))
    rm -f "${path}" "${path}.sha256"
    VERIFIED=$((VERIFIED + 1))
  done
}

# Sends the load, checking files as they close.
run_load() {
  local loadgen
  ip netns exec "${GEN_NS}" "${LOADGEN}" "${TARGET_IP}" 9 "${PACKETS}" "${RATE}" "${PAYLOAD}" >"${WORK}/loadgen.out" &
  loadgen=$!
  PIDS+=("${loadgen}")
  while kill -0 "${loadgen}" 2>/dev/null; do
    verify_closed
    sleep 2
  done
  wait "${loadgen}" || fail "ics-loadgen failed: $(cat "${WORK}/loadgen.out")"
}

# Stops ics-capd, which closes its last file, and checks what is left.
stop_capd() {
  # The kernel hands over a part-filled ring block within 100 ms.
  sleep 1
  kill -TERM "${CAPD_PID}"
  wait "${CAPD_PID}" || fail "ics-capd exited with $?: $(tail -n 3 "${LOG}")"
  grep -q '"event":"stopped"' "${LOG}" || fail "ics-capd did not log stopping"
  verify_closed
}

judge() {
  local sent
  sent="$(sed -n 's/^sent \([0-9]*\)$/\1/p' "${WORK}/loadgen.out")"
  {
    echo "Sent:     ${sent} datagrams at ${RATE}/s"
    echo "Files:    ${VERIFIED}, each matching its .sha256"
    echo "Captured: ${CAPTURED} packets"
    echo "Drops:    ${DROPS}"
  } | tee "${SUMMARY}"
  ((sent == PACKETS)) || fail "ics-loadgen sent ${sent} of ${PACKETS}"
  ((DROPS == 0)) || fail "ics-capd counted ${DROPS} drops"
  ((CAPTURED == sent)) || fail "the files hold ${CAPTURED} packets, but ${sent} were sent"
  ! grep -q '"level":"error"' "${LOG}" || fail "ics-capd logged an error: $(grep '"level":"error"' "${LOG}" | head -n 1)"
  echo "Capture bench: ok; ${CAPTURED} packets in ${VERIFIED} files, none dropped"
}

main() {
  ((EUID == 0)) || fail "run as root: network and mount namespaces need it"
  [[ -x "${CAPD}" && -x "${LOADGEN}" ]] || fail "${CAPD} and ${LOADGEN} must be executables"
  [[ "${PACKETS}${RATE}${ROTATE_S}" =~ ^[0-9]+$ ]] || fail "PACKETS, RATE and ROTATE_SECONDS must be whole numbers"
  make_link
  start_capd
  wait_until "${SETTLE_S}" "ics-capd to start" capd_started
  run_load
  stop_capd
  judge
}

main
