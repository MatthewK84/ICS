#!/usr/bin/env bash
# Generate the golden frame and geoid conversion vectors with GeographicLib
# (ICS-014). See docs/frames-and-time.md for the conventions they pin down.
#
# Usage: golden/frames/generate.sh [--check]
#
# Reads the case lists in golden/frames/inputs and writes, beside this script:
#   geodetic-ecef.csv  WGS84 geodetic to ECEF (CartConvert)
#   geodetic-enu.csv   WGS84 geodetic to the east-north-up frame of an origin
#                      (CartConvert -l)
#   egm96-5.csv        EGM96 geoid height, and MSL height to ellipsoid height
#                      (GeoidEval, the egm96-5 grid, cubic interpolation)
#   utm-geodetic.csv   WGS84 UTM to latitude and longitude (GeoConvert;
#                      ICS-024)
# With --check, it writes them to a temporary folder instead and fails unless
# the committed files match exactly; CI runs this in the ics-cpp image.
#
# Needs GeographicLib's CartConvert, GeoConvert and GeoidEval and the egm96-5
# grid, which the ics-cpp image installs (deploy/toolchain). GeoidEval reads
# the grid from /usr/share/GeographicLib/geoids, or from
# $GEOGRAPHICLIB_GEOID_PATH if set.
set -euo pipefail
export LC_ALL=C

HERE="$(cd "$(dirname "$0")" && pwd)"
readonly HERE
readonly INPUTS="${HERE}/inputs"
readonly OUTPUTS=(geodetic-ecef.csv geodetic-enu.csv egm96-5.csv utm-geodetic.csv)
# CartConvert's digits after the decimal point: nanometres, far inside the
# 1 mm that ICS-017 must match.
readonly PRECISION=9
readonly GEOID=egm96-5
WORK="$(mktemp -d)"
readonly WORK
trap 'rm -rf "${WORK}"' EXIT

fail() {
  echo "::error::$*" >&2
  exit 1
}

require_tools() {
  command -v CartConvert >/dev/null || fail "CartConvert not found; run this in the ics-cpp image"
  command -v GeoidEval >/dev/null || fail "GeoidEval not found; run this in the ics-cpp image"
  command -v GeoConvert >/dev/null || fail "GeoConvert not found; run this in the ics-cpp image"
  echo "0 0" | GeoidEval -n "${GEOID}" >/dev/null || fail "GeoidEval cannot read the ${GEOID} grid"
}

# The rows of an input list, without its header.
rows() {
  tail -n +2 "${INPUTS}/$1"
}

# Columns $2 of input list $1, space-separated, one case per line.
columns() {
  rows "$1" | cut -d, -f "$2" | tr , ' '
}

# Join the input rows of list $1 with the tool output in file $2, one case per
# line, after checking that every case has an output.
join_rows() {
  local list="$1" output="$2"
  if [[ "$(rows "${list}" | wc -l)" != "$(wc -l <"${output}")" ]]; then
    fail "a GeographicLib tool did not answer every case in ${list}"
  fi
  paste -d, <(rows "${list}") "${output}"
}

write_ecef() {
  local out="$1"
  columns geodetic-points.csv 2-4 | CartConvert -p "${PRECISION}" | tr ' ' , >"${WORK}/ecef.txt"
  echo "id,latitude_deg,longitude_deg,height_ellipsoid_m,x_m,y_m,z_m" >"${out}"
  join_rows geodetic-points.csv "${WORK}/ecef.txt" >>"${out}"
}

write_enu() {
  local out="$1" lat0 lon0 h0 lat lon h enu
  : >"${WORK}/enu.txt"
  while IFS=, read -r _ lat0 lon0 h0 lat lon h; do
    enu="$(echo "${lat} ${lon} ${h}" | CartConvert -l "${lat0}" "${lon0}" "${h0}" -p "${PRECISION}")"
    echo "${enu// /,}" >>"${WORK}/enu.txt"
  done < <(rows enu-cases.csv)
  echo "id,origin_latitude_deg,origin_longitude_deg,origin_height_ellipsoid_m,latitude_deg,longitude_deg,height_ellipsoid_m,east_m,north_m,up_m" >"${out}"
  join_rows enu-cases.csv "${WORK}/enu.txt" >>"${out}"
}

write_geoid() {
  local out="$1"
  columns geoid-points.csv 2-3 | GeoidEval -n "${GEOID}" >"${WORK}/geoid-height.txt"
  columns geoid-points.csv 2-4 | GeoidEval -n "${GEOID}" --msltohae | sed 's/.* //' >"${WORK}/hae.txt"
  paste -d, "${WORK}/geoid-height.txt" "${WORK}/hae.txt" >"${WORK}/geoid.txt"
  echo "id,latitude_deg,longitude_deg,msl_height_m,geoid_height_m,height_ellipsoid_m" >"${out}"
  join_rows geoid-points.csv "${WORK}/geoid.txt" >>"${out}"
}

# GeoConvert reads "<zone><hemisphere> <easting> <northing>" and, with -g,
# writes latitude and longitude in degrees, here to 14 decimal places.
write_utm() {
  local out="$1"
  rows utm-points.csv | awk -F, '{print $2 $3, $4, $5}' | GeoConvert -g -p "${PRECISION}" | tr ' ' , >"${WORK}/utm.txt"
  echo "id,zone,hemisphere,easting_m,northing_m,latitude_deg,longitude_deg" >"${out}"
  join_rows utm-points.csv "${WORK}/utm.txt" >>"${out}"
}

main() {
  local mode="${1:-}" target="${HERE}" file
  if [[ -n "${mode}" && "${mode}" != "--check" ]]; then
    fail "usage: golden/frames/generate.sh [--check]"
  fi
  require_tools
  if [[ "${mode}" == "--check" ]]; then
    target="${WORK}"
  fi
  write_ecef "${target}/geodetic-ecef.csv"
  write_enu "${target}/geodetic-enu.csv"
  write_geoid "${target}/egm96-5.csv"
  write_utm "${target}/utm-geodetic.csv"
  if [[ "${mode}" != "--check" ]]; then
    echo "Wrote ${OUTPUTS[*]} in ${HERE}"
    return
  fi
  for file in "${OUTPUTS[@]}"; do
    diff -u "${HERE}/${file}" "${WORK}/${file}" || fail "golden/frames/${file} is stale; run golden/frames/generate.sh"
  done
  echo "Golden frame vectors: all ${#OUTPUTS[@]} files match GeographicLib $(CartConvert --version | sed 's/.* //')"
}

main "$@"
