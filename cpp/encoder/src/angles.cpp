#include "ics/encoder/angles.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "ics/common/check.hpp"
#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/encoder/pointing_model.hpp"
#include "ics/tagger/datagram.hpp"
#include "ics/tagger/pps_clock.hpp"
#include "ics/tagger/sampler.hpp"

namespace ics::encoder {
namespace {

// An axis's angle from its counts, in radians from the encoder's zero.
[[nodiscard]] double axis_angle(const std::int32_t counts, const AxisConfig& axis) noexcept {
  const double angle = tagger::angle_rad(counts, axis.counts_per_rev);
  return axis.reversed ? -angle : angle;
}

}  // namespace

std::string_view to_string(const tagger::ClockState state) noexcept {
  constexpr std::array<std::string_view, 3> kNames{"acquiring", "locked", "holdover"};
  const auto index = static_cast<std::size_t>(state);
  return check(index < kNames.size()) ? kNames.at(index) : std::string_view("unknown");
}

bool valid(const EncoderConfig& config) noexcept {
  return config.azimuth.counts_per_rev >= kMinCountsPerRev && config.elevation.counts_per_rev >= kMinCountsPerRev;
}

Result<AxisAngles> angles_from(const tagger::Sample& sample, const EncoderConfig& config) noexcept {
  if (!valid(config)) {
    return fail(Error::kInvalidArgument);
  }
  if (sample.state == tagger::ClockState::kAcquiring) {
    return fail(Error::kUnconstrained);
  }
  return AxisAngles{.utc = utc_from_ns(sample.utc_ns),
                    .bound = Duration(sample.bound_ns),
                    .encoder = Direction{.azimuth_rad = wrap_turn(axis_angle(sample.azimuth_counts, config.azimuth)),
                                         .elevation_rad = wrap_pi(axis_angle(sample.elevation_counts, config.elevation))},
                    .state = sample.state};
}

}  // namespace ics::encoder
