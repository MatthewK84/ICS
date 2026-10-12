#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"

namespace ics::mount {

// A station mount's axes (ICS-033). The stations' mounts are alt-az: PWI4's
// axis0 is azimuth and its axis1 is altitude.
enum class Axis : std::uint8_t { kAzimuth = 0, kAltitude = 1 };
inline constexpr std::size_t kAxes = 2;

// One axis, as PWI4 reports it, in the axis's own coordinates: azimuth
// counts on past a turn through the mount's cable wrap.
struct AxisStatus {
  bool enabled = false;
  Radians position{};
  double velocity_rad_per_s = 0.0;
  // The mount's mechanical limits.
  Radians min_position{};
  Radians max_position{};
};

// The mount, as PWI4's /status reports it.
struct MountStatus {
  bool connected = false;
  bool slewing = false;
  bool tracking = false;
  // The angle between where the mount points and the sun's centre.
  Radians distance_to_sun{};
  // By Axis.
  std::array<AxisStatus, kAxes> axes{};
};

// The largest /status text read; PWI4's is a few kilobytes.
inline constexpr std::size_t kMaxStatusBytes = 64 * 1024;

// Reads PWI4's /status text: keyword=value lines (PWI4 4.0.13 or later).
// Keywords it does not use are ignored, since PWI4 adds keywords between
// versions. Error::kMalformed for text over kMaxStatusBytes, a line without
// '=', or a keyword it uses that is missing, or whose value is not a
// "true" or "false", or a number, as the keyword needs.
[[nodiscard]] Result<MountStatus> parse_status(std::string_view text);

}  // namespace ics::mount
