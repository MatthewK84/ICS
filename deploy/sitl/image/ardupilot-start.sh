#!/usr/bin/env bash
# Start ArduCopter SITL as a quadrotor for the ICS SITL rig (ICS-018).
#
# Environment: ICS_HOME_LAT and ICS_HOME_LON (degrees) and ICS_HOME_ALT
# (metres above MSL) place the vehicle; ICS_RIG_ADDRESS and ICS_RIG_PORT
# are where it sends MAVLink. ArduCopter is MAVLink system 2
# and starts from its default parameters each time.
set -euo pipefail

readonly HOME_LOCATION="${ICS_HOME_LAT:?ICS_HOME_LAT must be set},${ICS_HOME_LON:?ICS_HOME_LON must be set},${ICS_HOME_ALT:?ICS_HOME_ALT must be set},0"
readonly RIG="udpclient:${ICS_RIG_ADDRESS:?ICS_RIG_ADDRESS must be set}:${ICS_RIG_PORT:?ICS_RIG_PORT must be set}"
exec arducopter --model quad --speedup 1 --sysid 2 --wipe --home "${HOME_LOCATION}" \
  --defaults /opt/ardupilot/copter.parm --serial0 "${RIG}"
