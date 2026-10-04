#include "ics/sapient/registration.hpp"

#include <optional>

#include "ics/common/error.hpp"
#include "ics/sapient/location.hpp"

namespace ics::sapient {
namespace {

namespace bsi = sapient_msg::bsi_flex_335_v2_0;

constexpr double kKilometresPerHourInMps = 1.0 / 3.6;

// One value that a set of definitions must agree on.
template <typename T>
class Agreed {
 public:
  void add(const T& value) noexcept {
    conflicting_ = conflicting_ || (value_.has_value() && !(*value_ == value));
    value_ = value;
  }
  void reject() noexcept { conflicting_ = true; }
  [[nodiscard]] std::optional<T> value() const noexcept { return conflicting_ ? std::nullopt : value_; }

 private:
  std::optional<T> value_;
  bool conflicting_ = false;
};

// Metres per second per unit of a speed, if the unit is known.
[[nodiscard]] std::optional<double> mps(const bsi::SpeedUnits units) noexcept {
  if (units == bsi::SPEED_UNITS_MS) {
    return 1.0;
  }
  if (units == bsi::SPEED_UNITS_KPH) {
    return kKilometresPerHourInMps;
  }
  return std::nullopt;
}

void read_zone(const Registration::LocationType& type, Agreed<UtmZone>& zone) noexcept {
  if (type.location_units() != bsi::LOCATION_COORDINATE_SYSTEM_UTM_M || !type.has_zone()) {
    return;
  }
  const Result<UtmZone> parsed = parse_zone(type.zone());
  if (parsed) {
    zone.add(*parsed);
  } else {
    zone.reject();
  }
}

void read_velocity(const Registration::VelocityType& type, Agreed<VelocityUnits>& velocity) noexcept {
  if (!type.has_enu_velocity_units()) {
    return;
  }
  const bsi::ENUVelocityUnits& units = type.enu_velocity_units();
  const std::optional<double> horizontal = mps(units.east_north_rate_units());
  const std::optional<double> vertical = units.has_up_rate_units() ? mps(units.up_rate_units()) : horizontal;
  if (horizontal && vertical) {
    velocity.add(VelocityUnits{.horizontal_mps = *horizontal, .vertical_mps = *vertical});
  } else {
    velocity.reject();
  }
}

}  // namespace

NodeUnits read_units(const Registration& registration) noexcept {
  Agreed<UtmZone> zone;
  Agreed<VelocityUnits> velocity;
  for (const Registration::ModeDefinition& mode : registration.mode_definition()) {
    for (const Registration::DetectionDefinition& definition : mode.detection_definition()) {
      read_zone(definition.location_type(), zone);
      if (definition.has_velocity_type()) {
        read_velocity(definition.velocity_type(), velocity);
      }
    }
  }
  return NodeUnits{.zone = zone.value(), .velocity = velocity.value()};
}

}  // namespace ics::sapient
