#pragma once

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"

namespace ics::frames {

// A WGS84 geodetic position (ICS-017; docs/frames-and-time.md): geodetic
// latitude from -90 to 90 degrees, longitude in [-180, 180) degrees, and
// height above the ellipsoid. Every value is finite. A Geodetic can only be
// made through make(), so a function taking one never has to check it.
class Geodetic {
 public:
  // Fails with Error::kInvalidArgument when a value is not finite, and with
  // Error::kOutOfRange when the latitude is beyond 90 degrees either way.
  // The longitude is wrapped into [-180, 180), so 180 becomes -180.
  [[nodiscard]] static Result<Geodetic> make(Degrees latitude, Degrees longitude, Meters height) noexcept;

  [[nodiscard]] Degrees latitude() const noexcept { return latitude_; }
  [[nodiscard]] Degrees longitude() const noexcept { return longitude_; }
  [[nodiscard]] Meters height() const noexcept { return height_; }

 private:
  Geodetic(Degrees latitude, Degrees longitude, Meters height) noexcept;

  Degrees latitude_;
  Degrees longitude_;
  Meters height_;
};

// A vector in Earth-centred, Earth-fixed axes: Z to the north pole, X to
// latitude 0 and longitude 0, Y to longitude 90 degrees east.
template <typename Unit>
struct EcefVector {
  Quantity<Unit> x;
  Quantity<Unit> y;
  Quantity<Unit> z;
};

// An ECEF position, from the Earth's centre.
using Ecef = EcefVector<unit::Meter>;

// The ECEF position of a geodetic point; exact at the poles and the equator.
[[nodiscard]] Ecef to_ecef(const Geodetic& point) noexcept;

// The geodetic point at an ECEF position, accurate to nanometres everywhere,
// including at the poles and near the Earth's centre. On the polar axis the
// longitude is arbitrary. Fails with Error::kInvalidArgument when a
// coordinate is not finite, or the point is too far away for its height to
// be finite.
[[nodiscard]] Result<Geodetic> to_geodetic(const Ecef& position) noexcept;

}  // namespace ics::frames
