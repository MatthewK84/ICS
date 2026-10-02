#!/usr/bin/env bash
# Start PX4 SITL as a SIH quadrotor for the ICS SITL rig (ICS-018).
#
# Environment: ICS_HOME_LAT and ICS_HOME_LON (degrees) and ICS_HOME_ALT
# (metres above MSL) place the vehicle; ICS_RIG_ADDRESS and ICS_RIG_PORT are
# where it sends MAVLink (px4-rc.mavlink). PX4 is MAVLink system 1, and its
# ground-station link is UDP port 18570.
set -euo pipefail

export PX4_SYS_AUTOSTART=10040
export PX4_SIMULATOR=sihsim
export PX4_SIM_MODEL=quadx
export PX4_HOME_LAT="${ICS_HOME_LAT:?ICS_HOME_LAT must be set}"
export PX4_HOME_LON="${ICS_HOME_LON:?ICS_HOME_LON must be set}"
export PX4_HOME_ALT="${ICS_HOME_ALT:?ICS_HOME_ALT must be set}"
export ICS_RIG_ADDRESS="${ICS_RIG_ADDRESS:?ICS_RIG_ADDRESS must be set}"
export ICS_RIG_PORT="${ICS_RIG_PORT:?ICS_RIG_PORT must be set}"
exec px4 -d -w /var/lib/px4 /opt/px4/etc
