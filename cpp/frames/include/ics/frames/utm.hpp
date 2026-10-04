#pragma once

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/frames/geodetic.hpp"

namespace ics::frames {

// A WGS84 UTM position (ICS-024): a zone from 1 to 60, its hemisphere, and
// an easting and northing with UTM's false easting of 500 km and, in the
// south, false northing of 10,000 km.
struct UtmPoint {
  int zone = 0;
  bool north = true;
  Meters easting{0.0};
  Meters northing{0.0};
};

// The geodetic point of a UTM position, at the height given. It uses
// Karney's sixth-order Krüger series ("Transverse Mercator with an accuracy
// of a few nanometers", J. Geodesy 85, 2011), as GeographicLib's
// TransverseMercator does, so it is good to nanometres across a zone; the
// tests match GeographicLib's GeoConvert (golden/frames/utm-geodetic.csv).
// Fails with Error::kInvalidArgument for a zone outside 1 to 60, a height
// that is not finite, or an easting outside 0 to 1,000 km, or a northing
// outside 0 to 9,600 km in the north or 1,000 to 10,000 km in the south:
// UTM's limits, beyond which the series lose their accuracy.
[[nodiscard]] Result<Geodetic> from_utm(const UtmPoint& point, Meters height) noexcept;

}  // namespace ics::frames
