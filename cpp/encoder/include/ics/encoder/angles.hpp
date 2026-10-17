#pragma once

#include <cstdint>
#include <string_view>

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/encoder/pointing_model.hpp"
#include "ics/tagger/pps_clock.hpp"
#include "ics/tagger/sampler.hpp"

namespace ics::encoder {

// One axis's encoder: absolute counts, linear in angle from the encoder's own
// zero. The pointing model's IA and IE carry the zero's offset from north
// and the horizon.
struct AxisConfig {
  // Counts a revolution: 2^24 or more keeps a count under 0.4 urad.
  std::uint32_t counts_per_rev = 0;
  // True when the counts fall as the angle grows: azimuth through east,
  // elevation up.
  bool reversed = false;
};

// Both axes' encoders, as wired to the time-tagger (ICS-032).
struct EncoderConfig {
  AxisConfig azimuth;
  AxisConfig elevation;
};

// The smallest counts_per_rev an axis may have.
inline constexpr std::uint32_t kMinCountsPerRev = 2;

// Whether both axes have kMinCountsPerRev counts a revolution or more.
[[nodiscard]] bool valid(const EncoderConfig& config) noexcept;

// The clock state's name: "acquiring", "locked" or "holdover"; "unknown"
// for a value not listed.
[[nodiscard]] std::string_view to_string(tagger::ClockState state) noexcept;

// One sample's axis angles, with its time.
struct AxisAngles {
  UtcTime utc;
  // The most utc may be off.
  Duration bound{};
  // The azimuth in [0, 2 pi) and the elevation in [-pi, pi), so that an
  // encoder that counts one revolution and wraps reads the same as one that
  // counts on.
  Direction encoder;
  tagger::ClockState state = tagger::ClockState::kAcquiring;
};

// A sample's axis angles. kUnconstrained for a sample whose clock is still
// acquiring, which has no time; kInvalidArgument for a config that is not
// valid().
[[nodiscard]] Result<AxisAngles> angles_from(const tagger::Sample& sample, const EncoderConfig& config) noexcept;

}  // namespace ics::encoder
