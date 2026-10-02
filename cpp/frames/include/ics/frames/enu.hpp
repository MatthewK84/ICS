#pragma once

#include <array>

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/frames/geodetic.hpp"

namespace ics::frames {

// A vector in east-north-up axes.
template <typename Unit>
struct EnuVector {
  Quantity<Unit> east;
  Quantity<Unit> north;
  Quantity<Unit> up;
};

// An ENU position, from the frame's origin.
using Enu = EnuVector<unit::Meter>;

// The range ENU frame (ICS-017; docs/frames-and-time.md): origin at a
// geodetic point, up along the ellipsoid normal there, east and north tangent
// to its parallel and meridian. This is GeographicLib's LocalCartesian.
// Every member is noexcept and allocates nothing, for real-time paths.
class EnuFrame {
 public:
  explicit EnuFrame(const Geodetic& origin) noexcept;

  [[nodiscard]] const Geodetic& origin() const noexcept { return origin_; }

  [[nodiscard]] Enu to_enu(const Ecef& position) const noexcept;
  [[nodiscard]] Enu to_enu(const Geodetic& point) const noexcept { return to_enu(frames::to_ecef(point)); }
  [[nodiscard]] Ecef to_ecef(const Enu& position) const noexcept;
  // Fails as frames::to_geodetic does, for a position too far to represent.
  [[nodiscard]] Result<Geodetic> to_geodetic(const Enu& position) const noexcept {
    return frames::to_geodetic(to_ecef(position));
  }

  // Rotates a vector, such as a velocity, between ECEF and ENU axes. Unlike a
  // position, a vector does not move with the origin.
  template <typename Unit>
  [[nodiscard]] EnuVector<Unit> rotate(const EcefVector<Unit>& vector) const noexcept {
    const std::array<double, 3> enu = to_enu_axes({vector.x.value(), vector.y.value(), vector.z.value()});
    return {Quantity<Unit>(enu[0]), Quantity<Unit>(enu[1]), Quantity<Unit>(enu[2])};
  }
  template <typename Unit>
  [[nodiscard]] EcefVector<Unit> rotate(const EnuVector<Unit>& vector) const noexcept {
    const std::array<double, 3> ecef = to_ecef_axes({vector.east.value(), vector.north.value(), vector.up.value()});
    return {Quantity<Unit>(ecef[0]), Quantity<Unit>(ecef[1]), Quantity<Unit>(ecef[2])};
  }

 private:
  [[nodiscard]] std::array<double, 3> to_enu_axes(const std::array<double, 3>& ecef) const noexcept;
  [[nodiscard]] std::array<double, 3> to_ecef_axes(const std::array<double, 3>& enu) const noexcept;

  Geodetic origin_;
  Ecef origin_ecef_;
  // GeographicLib's layout: the ECEF components of the east, north and up
  // unit vectors are (rotation_[0], [3], [6]), ([1], [4], [7]) and
  // ([2], [5], [8]).
  std::array<double, 9> rotation_;
};

}  // namespace ics::frames
