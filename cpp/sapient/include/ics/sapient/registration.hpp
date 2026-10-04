#pragma once

#include <optional>

#include "ics/sapient/location.hpp"
#include "sapient_msg/bsi_flex_335_v2_0/registration.pb.h"

namespace ics::sapient {

using Registration = sapient_msg::bsi_flex_335_v2_0::Registration;

// The factors from a node's ENU velocity units to metres per second.
struct VelocityUnits {
  // For the east and north rates.
  double horizontal_mps = 1.0;
  // For the up rate.
  double vertical_mps = 1.0;

  [[nodiscard]] friend bool operator==(const VelocityUnits& a, const VelocityUnits& b) noexcept {
    return a.horizontal_mps == b.horizontal_mps && a.vertical_mps == b.vertical_mps;
  }
};

// What a node's registration says about the detections it will send
// (ICS-024).
struct NodeUnits {
  // The UTM zone of its detections' locations, unless a location gives its
  // own.
  std::optional<UtmZone> zone;
  // The units of its detections' ENU velocities.
  std::optional<VelocityUnits> velocity;
};

// Reads the units of a registration's detection definitions, in all its
// modes. A detection does not say which mode it was made in, so a zone, or a
// velocity unit, is known only when every definition that gives one gives
// the same; otherwise it is left unset. Speeds are in m/s (SPEED_UNITS_MS) or
// km/h (SPEED_UNITS_KPH), and an up rate without units of its own takes the
// east and north rates' units.
[[nodiscard]] NodeUnits read_units(const Registration& registration) noexcept;

}  // namespace ics::sapient
