#!/usr/bin/env bash
# Store one SITL run's capture with ics-plid and query it back (ICS-030).
#
# Usage: plid-check.sh RUN_DIR BUILD_DIR
#
# Runs in the ics-cpp image, which holds the EGM96 grid. Writes
# /etc/ics/ics-plid.toml for a store in RUN_DIR/pli fed by RUN_DIR/tap.pcap,
# with the range origin and settings ics-mavlink-replay uses (no roles),
# starts BUILD_DIR's ics-plid, waits until it has replayed the capture, asks
# for every record and every event with ics-pli-query into
# RUN_DIR/plid-store.jsonl, and stops ics-plid with SIGTERM, which archives
# the store as Parquet. ics-plid's log is RUN_DIR/plid.log. python -m ics_sitl
# store-check then compares the answer with the replay.
set -euo pipefail

readonly RUN_DIR="${1:?usage: plid-check.sh RUN_DIR BUILD_DIR}"
readonly BUILD_DIR="${2:?usage: plid-check.sh RUN_DIR BUILD_DIR}"
readonly PLID="${BUILD_DIR}/services/plid/ics-plid"
readonly QUERY="${BUILD_DIR}/services/plid/ics-pli-query"
readonly LOG="${RUN_DIR}/plid.log"
readonly ANSWER="${RUN_DIR}/plid-store.jsonl"
# Tenths of a second to wait for the replay.
readonly WAIT_TENTHS=600

mkdir -p /etc/ics /run/ics-plid "${RUN_DIR}/pli"
cat >/etc/ics/ics-plid.toml <<CONFIG
[log]
level = "info"
service = "ics-plid"

[plid]
store_folder = "${RUN_DIR}/pli"
query_socket = "/run/ics-plid/query"
rotate_interval_ns = 3_600_000_000_000
sync_interval_ns = 1_000_000_000
feeds = ["mavlink"]

[range]
latitude_deg = 40.0
longitude_deg = -100.0
height_m = 700.0

[capture]
interfaces = []
files = ["${RUN_DIR}/tap.pcap"]

[mavlink]
roles = []
link_timeout_ns = 3_000_000_000
max_age_ns = 1_000_000_000
CONFIG

"${PLID}" 2>"${LOG}" &
plid=$!
for _ in $(seq "${WAIT_TENTHS}"); do
  if grep -q '"event":"replayed"' "${LOG}" || ! kill -0 "${plid}" 2>/dev/null; then
    break
  fi
  sleep 0.1
done
status=0
if grep -q '"event":"replayed"' "${LOG}"; then
  "${QUERY}" records >"${ANSWER}" || status=1
  "${QUERY}" events >>"${ANSWER}" || status=1
else
  echo "ics-plid did not replay ${RUN_DIR}tap.pcap; see ${LOG}" >&2
  status=1
fi
kill -TERM "${plid}" 2>/dev/null || true
wait "${plid}" || status=1
# The container runs as root; the runner keeps the records as its own user.
chmod -R a+rX "${RUN_DIR}/pli"
exit "${status}"
