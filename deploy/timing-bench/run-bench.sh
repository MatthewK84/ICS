#!/usr/bin/env bash
# The emulated timing bench (ICS-019 "Done when": holdover is flagged within
# 1 s of GNSS loss, in 20 of 20 trials).
#
# Usage: run-bench.sh ICS_TIMINGD [TRIALS]
#
# Two network namespaces joined by a veth pair: one runs a software ptp4l
# grandmaster (grandmaster.cfg), the other the station's ptp4l
# (station.cfg), which ICS_TIMINGD polls. Each trial waits a random 0 to
# 0.5 s, so trials fall at different points of the announce and poll cycles,
# then emulates GNSS loss: pmc sets the grandmaster's clockClass from 6 to 7
# and its timeSource from GNSS (0x20) to its internal oscillator (0xA0). The
# latency is the time from just before that pmc command to the "ts" of the
# "clock_state" holdover line ics-timingd logs. Then GNSS comes back and the
# next trial waits for "locked" again. Fails unless every trial is flagged
# within 1 s.
#
# ics-timingd reads its config from /etc/ics/ics-timingd.toml. The bench runs
# it in its own mount namespace with the bench's config mounted there, so the
# host's /etc/ics is never changed. An empty /etc/ics is made for the mount if
# there is none, and removed afterwards.
#
# Run as root: network and mount namespaces need it. Needs ip, ptp4l and pmc
# (apt-packages.txt), and unshare and mount. Prints one line per trial, and
# writes trials.csv and the logs of ptp4l and ics-timingd to $BENCH_OUT, if
# set.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
readonly HERE
readonly TIMINGD="${1:?usage: run-bench.sh ICS_TIMINGD [TRIALS]}"
readonly TRIALS="${2:-20}"
readonly LIMIT_NS=1000000000
# Seconds to wait for the bench to settle, and for each state change.
readonly SETTLE_S=60
readonly CHANGE_S=5
readonly GM_NS="ics-tb-gm"
readonly ST_NS="ics-tb-st"
# The folder holds the Unix sockets, so its path must be short.
WORK="$(mktemp -d /tmp/ics-tb.XXXXXX)"
readonly WORK
readonly LOG="${WORK}/ics-timingd.log"
readonly CSV="${WORK}/trials.csv"
readonly ETC_ICS="/etc/ics"
PIDS=()
MADE_ETC_ICS=0
PMC_CALLS=0

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
  ip netns del "${GM_NS}" 2>/dev/null || true
  ip netns del "${ST_NS}" 2>/dev/null || true
  if ((MADE_ETC_ICS == 1)); then
    rmdir "${ETC_ICS}" 2>/dev/null || true
  fi
  if [[ -n "${BENCH_OUT:-}" ]]; then
    mkdir -p "${BENCH_OUT}"
    cp "${WORK}"/*.log "${WORK}"/*.csv "${BENCH_OUT}/" 2>/dev/null || true
  fi
  rm -rf "${WORK}"
}
trap cleanup EXIT

# The grandmaster and the station, each in its own namespace, on a veth pair.
make_link() {
  ip netns add "${GM_NS}"
  ip netns add "${ST_NS}"
  ip link add ics-tb-gm0 netns "${GM_NS}" type veth peer name ics-tb-st0 netns "${ST_NS}"
  ip -n "${GM_NS}" link set lo up
  ip -n "${ST_NS}" link set lo up
  ip -n "${GM_NS}" link set ics-tb-gm0 up
  ip -n "${ST_NS}" link set ics-tb-st0 up
}

# start_ptp4l NAMESPACE CONFIG INTERFACE NAME: ptp4l with its management
# sockets at $WORK/NAME and $WORK/NAME-ro, logging to $WORK/NAME.log.
start_ptp4l() {
  local namespace="$1" config="$2" interface="$3" name="$4"
  ip netns exec "${namespace}" ptp4l -f "${HERE}/${config}" -i "${interface}" -m \
    --uds_address="${WORK}/${name}" --uds_ro_address="${WORK}/${name}-ro" >"${WORK}/${name}.log" 2>&1 &
  PIDS+=("$!")
}

# pmc_to SOCKET COMMAND: one management command to the ptp4l at $WORK/SOCKET.
pmc_to() {
  PMC_CALLS=$((PMC_CALLS + 1))
  pmc -u -b 0 -s "${WORK}/$1" -i "${WORK}/pmc.${PMC_CALLS}" "$2"
}

# set_gnss locked|lost: the grandmaster's clock quality with and without GNSS.
set_gnss() {
  local class=6 traceable=1 source=0x20
  if [[ "$1" == lost ]]; then
    class=7 traceable=0 source=0xA0
  fi
  pmc_to gm "SET GRANDMASTER_SETTINGS_NP clockClass ${class} clockAccuracy 0x21 offsetScaledLogVariance 0x4e5d \
currentUtcOffset 0 leap61 0 leap59 0 currentUtcOffsetValid 1 ptpTimescale 1 timeTraceable ${traceable} \
frequencyTraceable 1 timeSource ${source}" >/dev/null
}

# A station that never adjusts its clock (free_running) only reports its
# servo locked, and so its port SLAVE, once told its synchronization is
# certain (linuxptp 4.0 clock.c, clock_no_adjust).
mark_station_certain() {
  pmc_to st "SET SYNCHRONIZATION_UNCERTAIN_NP 0" >/dev/null
}

station_is_slave() {
  pmc_to st-ro "GET PORT_DATA_SET" | grep -qE "portState[[:space:]]+SLAVE"
}

# The number of clock_state lines ics-timingd has logged for STATE.
state_lines() {
  grep -c "\"event\":\"clock_state\",\"state\":\"$1\"" "${LOG}" || true
}

# more_state_lines STATE COUNT: true once there are more than COUNT.
more_state_lines() {
  (($(state_lines "$1") > $2))
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

# The time of the last clock_state line for STATE, in ns since the epoch.
last_state_ns() {
  local ts
  ts="$(grep "\"event\":\"clock_state\",\"state\":\"$1\"" "${LOG}" | tail -n 1 | sed -n 's/.*"ts":"\([^"]*\)".*/\1/p')"
  [[ -n "${ts}" ]] || fail "no clock_state $1 line in ${LOG}"
  date -u -d "${ts}" +%s%N
}

# Starts ics-timingd in its own mount namespace, with $WORK/etc-ics mounted
# over /etc/ics there.
start_timingd() {
  mkdir -p "${WORK}/etc-ics"
  cat >"${WORK}/etc-ics/ics-timingd.toml" <<EOF
[log]
level = "info"
service = "ics-timingd"

[timing]
station_id = "timing-bench"
ptp4l_socket = "${WORK}/st-ro"
client_socket = "${WORK}/timingd-client"
publish_socket = "${WORK}/time-quality"
camera_offsets_file = "${WORK}/camera-offsets.binpb"
ptp_domain = 0
poll_interval_ns = 100_000_000
asymmetry_bound_ns = 1_000
holdover_drift_ns_per_s = 50.0
EOF
  if [[ ! -d "${ETC_ICS}" ]]; then
    mkdir "${ETC_ICS}"
    MADE_ETC_ICS=1
  fi
  unshare --mount --propagation private sh -c "mount --bind \"\$1\" \"\$2\" && exec \"\$3\"" \
    sh "${WORK}/etc-ics" "${ETC_ICS}" "${TIMINGD}" 2>"${LOG}" &
  PIDS+=("$!")
}

# One trial: GNSS loss, then recovery. Appends "trial,latency_ns" to the CSV.
run_trial() {
  local trial="$1" holdovers locks start detected latency
  sleep "0.$(printf '%03d' $((RANDOM % 500)))"
  holdovers="$(state_lines holdover)"
  start="$(date -u +%s%N)"
  set_gnss lost
  wait_until "${CHANGE_S}" "holdover in trial ${trial}" more_state_lines holdover "${holdovers}"
  detected="$(last_state_ns holdover)"
  latency=$((detected - start))
  echo "${trial},${latency}" >>"${CSV}"
  printf 'Trial %2d: holdover flagged %4d ms after GNSS loss\n' "${trial}" $((latency / 1000000))
  locks="$(state_lines locked)"
  set_gnss locked
  wait_until "${CHANGE_S}" "relock in trial ${trial}" more_state_lines locked "${locks}"
}

# Fails unless every trial is within the limit.
judge() {
  local worst
  worst="$(cut -d, -f2 "${CSV}" | sort -n | tail -n 1)"
  echo "Worst of ${TRIALS} trials: $((worst / 1000000)) ms (limit $((LIMIT_NS / 1000000)) ms)"
  ((worst <= LIMIT_NS)) || fail "holdover took $((worst / 1000000)) ms to flag, over the limit"
  echo "Timing bench: ok; holdover flagged within 1 s in ${TRIALS} of ${TRIALS} trials"
}

main() {
  local trial
  ((EUID == 0)) || fail "run as root: network namespaces need it"
  [[ -x "${TIMINGD}" ]] || fail "${TIMINGD} is not an executable"
  [[ "${TRIALS}" =~ ^[1-9][0-9]*$ ]] || fail "TRIALS must be a positive integer, not ${TRIALS}"
  make_link
  start_ptp4l "${GM_NS}" grandmaster.cfg ics-tb-gm0 gm
  start_ptp4l "${ST_NS}" station.cfg ics-tb-st0 st
  wait_until "${SETTLE_S}" "the grandmaster's management socket" test -S "${WORK}/gm"
  wait_until "${SETTLE_S}" "the station's management socket" test -S "${WORK}/st"
  set_gnss locked
  mark_station_certain
  wait_until "${SETTLE_S}" "the station to follow the grandmaster" station_is_slave
  start_timingd
  wait_until "${SETTLE_S}" "ics-timingd to report locked" more_state_lines locked 0
  for ((trial = 1; trial <= TRIALS; trial++)); do
    run_trial "${trial}"
  done
  judge
}

main
