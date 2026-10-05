#!/usr/bin/env bash
# Fly the SITL rig's scripted engagements and check each one (ICS-018).
#
# Usage: run-engagements.sh OUT_DIR [ENGAGEMENT...]
#
# With no ENGAGEMENT, flies every deploy/sitl/engagements/*.toml. For each it
# starts both autopilots (compose.yaml), mirrors their traffic to the emulated
# TAP port and captures it there, flies the engagement with python -m
# ics_sitl, and checks the capture holds every datagram the rig counted. Every
# engagement is flown even if one fails; the script then exits 1. OUT_DIR/NAME
# keeps each engagement's truth.jsonl, summary.json, tap.pcap, tap-counts.json,
# container logs and both autopilots' onboard logs.
#
# Needs docker with compose, Python 3.12 (PYTHON, default python3), and root
# for ip, tc and tcpdump (through sudo unless already root). The images are
# ICS_PX4_IMAGE and ICS_ARDUPILOT_IMAGE (compose.yaml has the defaults).
set -euo pipefail

readonly OUT_DIR="${1:?usage: run-engagements.sh OUT_DIR [ENGAGEMENT...]}"
shift
HERE="$(cd "$(dirname "$0")" && pwd)"
readonly HERE
ROOT="$(cd "${HERE}/../.." && pwd)"
readonly ROOT
readonly PYTHON="${PYTHON:-python3}"
readonly COMPOSE=(docker compose --file "${HERE}/compose.yaml")
# The autopilots' addresses, and where the rig listens for each (compose.yaml).
readonly PX4_ADDRESS="172.30.18.10"
readonly ARDUPILOT_ADDRESS="172.30.18.20"
readonly RIG_FOR_PX4="172.30.18.1:14551"
readonly RIG_FOR_ARDUPILOT="172.30.18.1:14550"
SUDO=()
if ((EUID != 0)); then
  SUDO=(sudo)
fi
readonly SUDO

rig() {
  PYTHONPATH="${ROOT}/python" "${PYTHON}" -m ics_sitl "$@"
}

count() {
  "${SUDO[@]}" tcpdump -r "$1" -nn "$2" 2>/dev/null | wc -l
}

# How many UDP datagrams the capture holds from and to each autopilot.
tap_counts() {
  local pcap="$1"
  printf '{"px4": {"from": %d, "to": %d}, "ardupilot": {"from": %d, "to": %d}}\n' \
    "$(count "${pcap}" "udp and src host ${PX4_ADDRESS}")" "$(count "${pcap}" "udp and dst host ${PX4_ADDRESS}")" \
    "$(count "${pcap}" "udp and src host ${ARDUPILOT_ADDRESS}")" "$(count "${pcap}" "udp and dst host ${ARDUPILOT_ADDRESS}")"
}

start_capture() {
  local pcap="$1"
  "${SUDO[@]}" "${HERE}/tap.sh" capture "${pcap}" 2>"${pcap%.pcap}.tcpdump.log" &
  CAPTURE_PID=$!
  local waited
  for waited in $(seq 1 50); do
    if grep -q "listening on" "${pcap%.pcap}.tcpdump.log"; then
      return
    fi
    sleep 0.1
  done
  echo "error: tcpdump did not start after ${waited} tries" >&2
  return 1
}

# The rig has counted everything it received by the time it exits, but tcpdump
# may still hold the last frames in its buffer; give it a moment to write them.
stop_capture() {
  sleep 2
  "${SUDO[@]}" kill -INT "${CAPTURE_PID}"
  wait "${CAPTURE_PID}" || true
}

# Copy each autopilot's onboard log out of its container before the containers
# go (ICS-025): PX4 writes ULog files under log/ in its working directory, and
# ArduCopter DataFlash files under logs/. An autopilot that never armed has
# logged nothing, so a missing folder is not an error.
save_onboard_logs() {
  local out="$1"
  docker cp ics-sitl-px4:/var/lib/px4/log "${out}/px4-log" >/dev/null 2>&1 || true
  docker cp ics-sitl-ardupilot:/var/lib/ardupilot/logs "${out}/ardupilot-logs" >/dev/null 2>&1 || true
}

stop_all() {
  "${COMPOSE[@]}" down --timeout 5 >/dev/null 2>&1 || true
  "${SUDO[@]}" "${HERE}/tap.sh" down
}

# Fly one engagement; returns 1 if it or its TAP check fails.
fly() {
  local engagement="$1" out status=0
  out="${OUT_DIR}/$(basename "${engagement}" .toml)"
  mkdir -p "${out}"
  # Inside a function called with ||, bash ignores set -e, so each step checks.
  rig env "${engagement}" > "${out}/containers.env" || return 1
  "${COMPOSE[@]}" --env-file "${out}/containers.env" up --detach --quiet-pull || return 1
  "${SUDO[@]}" "${HERE}/tap.sh" up ics-sitl-px4 ics-sitl-ardupilot || return 1
  start_capture "${out}/tap.pcap" || return 1
  rig run "${engagement}" --px4 "${RIG_FOR_PX4}" --ardupilot "${RIG_FOR_ARDUPILOT}" --out "${out}" || status=1
  stop_capture
  save_onboard_logs "${out}"
  "${COMPOSE[@]}" --env-file "${out}/containers.env" logs --no-color > "${out}/containers.log" 2>&1 || true
  stop_all
  tap_counts "${out}/tap.pcap" > "${out}/tap-counts.json"
  rig tap-check --summary "${out}/summary.json" --counts "${out}/tap-counts.json" || status=1
  return "${status}"
}

main() {
  local engagements=("$@") failed=()
  if ((${#engagements[@]} == 0)); then
    engagements=("${HERE}"/engagements/*.toml)
  fi
  trap stop_all EXIT
  stop_all
  local engagement
  for engagement in "${engagements[@]}"; do
    echo "== $(basename "${engagement}" .toml)"
    fly "${engagement}" || failed+=("$(basename "${engagement}" .toml)")
  done
  if ((${#failed[@]} > 0)); then
    echo "Failed: ${failed[*]}" >&2
    exit 1
  fi
  echo "Every engagement passed: ${#engagements[@]} flown"
}

main "$@"
