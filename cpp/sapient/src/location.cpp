#include "ics/sapient/location.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <optional>
#include <string_view>

#include "ics/common/check.hpp"
#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/frames/egm96.hpp"
#include "ics/frames/geodetic.hpp"
#include "ics/frames/utm.hpp"
#include "ics/frames/wgs84.hpp"

namespace ics::sapient {
namespace {

namespace bsi = sapient_msg::bsi_flex_335_v2_0;

// The UTM latitude bands, each 8 degrees from 80 S, except X, which is 12.
constexpr std::string_view kBands = "CDEFGHJKLMNPQRSTUVWX";
constexpr double kBandHeightDeg = 8.0;
constexpr double kSouthernmostDeg = -80.0;
constexpr double kNorthernmostDeg = 84.0;
// About 110 km: room for an object just across a band edge from the sensor
// whose band it is reported in.
constexpr double kBandMarginDeg = 1.0;
constexpr int kZones = 60;
constexpr int kDecimal = 10;
constexpr double kRadiansPerDegree = std::numbers::pi / 180.0;

// A location's horizontal position, and the metres on the ground per unit
// of x and of y there.
struct Placed {
  frames::Geodetic point;
  double east_m_per_unit = 1.0;
  double north_m_per_unit = 1.0;
};

[[nodiscard]] bool in_band(const UtmZone& zone, const Degrees latitude) noexcept {
  if (zone.band == 'N') {
    return true;
  }
  const std::size_t index = kBands.find(zone.band);
  static_cast<void>(check(index != std::string_view::npos));
  const double south = kSouthernmostDeg + (kBandHeightDeg * static_cast<double>(index));
  const double north = zone.band == 'X' ? kNorthernmostDeg : south + kBandHeightDeg;
  return latitude.value() >= south - kBandMarginDeg && latitude.value() <= north + kBandMarginDeg;
}

// A latitude and longitude, with the metres per radian north and east there
// (the meridional radius and the parallel's radius) times the radians per
// unit.
[[nodiscard]] Result<Placed> angles(const Degrees latitude, const Degrees longitude,
                                    const double radians_per_unit) noexcept {
  const Result<frames::Geodetic> point = frames::Geodetic::make(latitude, longitude, Meters(0.0));
  if (!point) {
    return fail(Error::kInvalidArgument);
  }
  const double sin_latitude = std::sin(to_radians(latitude).value());
  const double w2 = 1.0 - (frames::wgs84::kEccentricitySquared * sin_latitude * sin_latitude);
  const double prime_vertical = frames::wgs84::kSemiMajorAxis.value() / std::sqrt(w2);
  const double meridional = prime_vertical * frames::wgs84::kOneMinusEccentricitySquared / w2;
  return Placed{.point = *point,
                .east_m_per_unit = prime_vertical * std::cos(to_radians(latitude).value()) * radians_per_unit,
                .north_m_per_unit = meridional * radians_per_unit};
}

// The location's own zone if it gives one, otherwise the default.
[[nodiscard]] Result<UtmZone> zone_of(const Location& location, const std::optional<UtmZone>& default_zone) noexcept {
  if (location.has_utm_zone() && !location.utm_zone().empty()) {
    return parse_zone(location.utm_zone());
  }
  if (default_zone) {
    return *default_zone;
  }
  return fail(Error::kInvalidArgument);
}

[[nodiscard]] Result<Placed> grid(const Location& location, const std::optional<UtmZone>& default_zone) noexcept {
  const Result<UtmZone> zone = zone_of(location, default_zone);
  if (!zone) {
    return fail(Error::kInvalidArgument);
  }
  const frames::UtmPoint utm{.zone = zone->number,
                             .north = zone->band >= 'N',
                             .easting = Meters(location.x()),
                             .northing = Meters(location.y())};
  const Result<frames::Geodetic> point = frames::from_utm(utm, Meters(0.0));
  if (!point || !in_band(*zone, point->latitude())) {
    return fail(Error::kInvalidArgument);
  }
  return Placed{.point = *point, .east_m_per_unit = 1.0, .north_m_per_unit = 1.0};
}

[[nodiscard]] Result<Placed> place(const Location& location, const std::optional<UtmZone>& default_zone) noexcept {
  switch (location.coordinate_system()) {
    case bsi::LOCATION_COORDINATE_SYSTEM_LAT_LNG_DEG_M:
      return angles(Degrees(location.y()), Degrees(location.x()), kRadiansPerDegree);
    case bsi::LOCATION_COORDINATE_SYSTEM_LAT_LNG_RAD_M:
      return angles(to_degrees(Radians(location.y())), to_degrees(Radians(location.x())), 1.0);
    case bsi::LOCATION_COORDINATE_SYSTEM_UTM_M:
      return grid(location, default_zone);
    default:
      return fail(Error::kInvalidArgument);
  }
}

// The point at the location's height, if its height is known.
[[nodiscard]] std::optional<frames::Geodetic> raise(const Location& location, const frames::Geodetic& point,
                                                    const frames::Egm96& geoid) noexcept {
  const Meters z(location.z());
  Result<frames::Geodetic> raised = fail(Error::kInvalidArgument);
  if (location.has_z() && location.datum() == bsi::LOCATION_DATUM_WGS84_E) {
    raised = frames::Geodetic::make(point.latitude(), point.longitude(), z);
  } else if (location.has_z() && location.datum() == bsi::LOCATION_DATUM_WGS84_G) {
    raised = geoid.from_msl(point.latitude(), point.longitude(), z);
  }
  return raised ? std::optional<frames::Geodetic>(*raised) : std::nullopt;
}

// An error in metres, from one in the coordinate's units, if it is usable.
[[nodiscard]] std::optional<double> metres(const bool has_error, const double error, const double m_per_unit) noexcept {
  const double converted = error * m_per_unit;
  if (!has_error || !std::isfinite(converted) || error < 0.0) {
    return std::nullopt;
  }
  return converted;
}

}  // namespace

Result<UtmZone> parse_zone(const std::string_view text) noexcept {
  if (text.size() < 2 || text.size() > 3) {
    return fail(Error::kInvalidArgument);
  }
  int number = 0;
  for (const char digit : text.substr(0, text.size() - 1)) {
    if (digit < '0' || digit > '9') {
      return fail(Error::kInvalidArgument);
    }
    number = (number * kDecimal) + (digit - '0');
  }
  const char band = static_cast<char>(std::toupper(static_cast<unsigned char>(text.back())));
  if (number < 1 || number > kZones || kBands.find(band) == std::string_view::npos) {
    return fail(Error::kInvalidArgument);
  }
  return UtmZone{.number = number, .band = band};
}

Result<Fix> to_fix(const Location& location, const std::optional<UtmZone>& default_zone,
                   const frames::Egm96& geoid) noexcept {
  if (!location.has_x() || !location.has_y()) {
    return fail(Error::kInvalidArgument);
  }
  const Result<Placed> placed = place(location, default_zone);
  if (!placed) {
    return fail(placed.error());
  }
  const std::optional<frames::Geodetic> raised = raise(location, placed->point, geoid);
  const std::optional<double> east = metres(location.has_x_error(), location.x_error(), placed->east_m_per_unit);
  const std::optional<double> north = metres(location.has_y_error(), location.y_error(), placed->north_m_per_unit);
  const std::optional<double> horizontal =
      east && north ? std::optional<double>(std::max(*east, *north)) : std::nullopt;
  const std::optional<double> vertical =
      raised ? metres(location.has_z_error(), location.z_error(), 1.0) : std::nullopt;
  return Fix{.point = raised.value_or(placed->point),
             .has_height = raised.has_value(),
             .horizontal_sigma_m = horizontal,
             .vertical_sigma_m = vertical};
}

}  // namespace ics::sapient
