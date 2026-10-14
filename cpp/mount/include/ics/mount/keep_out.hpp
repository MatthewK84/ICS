#pragma once

#include <numbers>
#include <optional>

#include "ics/common/units.hpp"
#include "ics/frames/geodetic.hpp"
#include "ics/mount/limits.hpp"
#include "ics/mount/pwi4_status.hpp"

namespace ics::mount {

// The sun keep-out (ICS-034): a cone around the sun's centre that the line of
// sight never enters, and a margin around it. The cone and margin together
// are the guard zone, which the interlock keeps the mount out of.
struct KeepOut {
  Radians cone{15.0 * std::numbers::pi / 180.0};
  // For pointing error, refraction and the sun's motion over a lookahead.
  Radians margin{2.0 * std::numbers::pi / 180.0};
  // How fast an escape moves the line of sight away from the sun, and how far
  // outside the guard zone it ends.
  double escape_rate_rad_per_s = std::numbers::pi / 180.0;
  Radians escape_clearance{std::numbers::pi / 180.0};
};

// The guard zone's radius: the cone and the margin.
[[nodiscard]] Radians guard(const KeepOut& keep_out) noexcept;

// Where the mount points, from its axis positions. Azimuth may count on past
// a turn, as the axis reports it.
[[nodiscard]] AltAz pointing(const MountStatus& status) noexcept;

// The angle between two directions.
[[nodiscard]] Radians separation(const AltAz& a, const AltAz& b) noexcept;

// A region of the sky an axis motion can cover: altitudes from low to high,
// and the azimuths from low to high, counted on past a turn as the azimuth
// axis does (a span of a full turn or more covers every azimuth).
struct Sweep {
  Radians altitude_low{};
  Radians altitude_high{};
  Radians azimuth_low{};
  Radians azimuth_high{};
};

// The least separation from the sun over a sweep. Each axis of an alt-az
// mount moves monotonically between two positions in a straight move, so a
// path between them stays in the sweep they span whatever the two axes'
// speeds.
[[nodiscard]] Radians closest_approach(const Sweep& sweep, const AltAz& sun) noexcept;

// Where the mount can be within the lookahead: the sweep from its position
// along its measured velocity, and along rates.
[[nodiscard]] Sweep sweep_ahead(const MountStatus& status, const AxisRates& rates, Duration lookahead) noexcept;

// Whether a motion may go on: it keeps out of the guard zone, or it comes no
// nearer the sun than the mount is now, so a mount inside the zone can
// always leave it.
[[nodiscard]] bool clear_ahead(const MountStatus& status, const AxisRates& rates, const AltAz& sun,
                               const KeepOut& keep_out, Duration lookahead) noexcept;

// Whether a goto may be sent: Error::kOutOfRange for a target inside the
// guard zone.
[[nodiscard]] Status check_goto_sun(const AltAz& target, const AltAz& sun, const KeepOut& keep_out) noexcept;

// Rates that move the line of sight straight away from the sun at the escape
// rate, held to the limits; nothing when the limits leave no way away.
[[nodiscard]] std::optional<AxisRates> escape_rates(const MountStatus& status, const AltAz& sun,
                                                    const MountLimits& limits, const KeepOut& keep_out) noexcept;

// The sun's greatest declination, rounded up: the obliquity of the ecliptic.
inline constexpr Radians kMaxSunDeclination{23.5 * std::numbers::pi / 180.0};

// Whether a fixed direction is outside the guard zone all year at a site:
// its declination is beyond the sun's by at least the guard zone, since the
// sun passes every hour angle each day.
[[nodiscard]] bool sun_safe_all_year(const AltAz& direction, const frames::Geodetic& site,
                                     const KeepOut& keep_out) noexcept;

}  // namespace ics::mount
