#pragma once

#include <optional>
#include <string_view>

#include "ics/common/error.hpp"
#include "ics/frames/egm96.hpp"
#include "ics/frames/geodetic.hpp"
#include "sapient_msg/bsi_flex_335_v2_0/location.pb.h"

namespace ics::sapient {

using Location = sapient_msg::bsi_flex_335_v2_0::Location;

// A UTM zone as SAPIENT writes it: the zone number, 1 to 60, then a latitude
// band letter, as in "30U", the zone of Dstl's sample messages.
struct UtmZone {
  int number = 0;
  // The band letter, in upper case: C to X, without I and O. C to M are
  // south of the equator and N to X north of it.
  char band = 'N';

  [[nodiscard]] friend bool operator==(const UtmZone& a, const UtmZone& b) noexcept {
    return a.number == b.number && a.band == b.band;
  }
};

// Reads a zone such as "30U" or "13s". Fails with Error::kInvalidArgument for
// anything else, such as a zone with no band letter, whose hemisphere is not
// known.
[[nodiscard]] Result<UtmZone> parse_zone(std::string_view text) noexcept;

// A SAPIENT location (ICS-024) as a point above the WGS84 ellipsoid, with its
// one-sigma errors in metres.
struct Fix {
  frames::Geodetic point;
  // Whether the point's height is known. When it is not, the height is 0.
  bool has_height = false;
  std::optional<double> horizontal_sigma_m;
  std::optional<double> vertical_sigma_m;
};

// Converts a location: x is the longitude or easting, y the latitude or
// northing, z the height.
// - LAT_LNG_DEG_M and LAT_LNG_RAD_M are in degrees or radians. UTM_M is in
//   the location's utm_zone, or the default zone (from the node's
//   registration) when it has none. The latitude must fall in the zone's
//   latitude band, give or take a degree, except for band N, which is read as
//   the northern hemisphere: some senders write "56S" for zone 56 south, and
//   the band check rejects most such points rather than placing them in the
//   wrong hemisphere.
// - z is above the ellipsoid for WGS84_E, and above mean sea level, the
//   EGM96 geoid, for WGS84_G. Without z, or a datum, the height is unknown.
// - x_error, y_error and z_error are one-sigma errors in the units of x, y
//   and z. The horizontal sigma is the larger of the x and y errors in
//   metres, and needs both. The vertical sigma is z_error, and needs a known
//   height. UTM errors are taken as metres on the ground.
// Fails with Error::kInvalidArgument for a location without x or y, with an
// unknown coordinate system, or with a position that is not a geodetic point
// or not in its UTM zone.
[[nodiscard]] Result<Fix> to_fix(const Location& location, const std::optional<UtmZone>& default_zone,
                                 const frames::Egm96& geoid) noexcept;

}  // namespace ics::sapient
