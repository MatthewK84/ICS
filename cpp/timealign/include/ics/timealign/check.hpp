#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/timealign/clock_fit.hpp"

namespace ics::timealign {

// ICS-026's "Done when": time alignment within 1 ms of the truth with an
// injected drift on SITL. The SITL rig's autopilots have no clock drift of
// their own, so the check puts one on a sortie's boot times, withholds part of
// its samples as a GNSS outage would, fits what is left and times each
// position, then compares each time with its reference.

// A drift and offset put on a boot clock, as a crystal running fast or slow
// would: boot' = offset + boot * (1 + ppm / 1e6).
struct Injection {
  double ppm = 0.0;
  Duration offset{};

  // Nothing unless the result is a boot time ICS takes.
  [[nodiscard]] std::optional<std::int64_t> apply(std::int64_t boot_us) const noexcept;
};

// The part of a sortie's samples to withhold, as fractions of their boot time
// span: from 0.2 to 0.8 withholds the middle 60 %.
struct Withholding {
  double from = 0.0;
  double to = 0.0;
};

struct DriftCheck {
  // The fit to the injected samples left.
  ClockFit fit;
  std::size_t positions = 0;
  // The largest difference between a position's aligned time and its
  // reference: its boot time plus the mean offset (UTC minus boot time) of the
  // samples as the vehicle sent them.
  Duration max_error{};
  // How far those offsets stray from their mean. The reference takes the
  // vehicle's clock to have no drift of its own, as in SITL: a large spread
  // says it has one, and the reference is wrong.
  Nanoseconds reference_spread{};
};

// Fails as fit_clock does, and with Error::kInvalidArgument when the
// injection or the fit takes a position outside the times ICS takes.
[[nodiscard]] Result<DriftCheck> check_injected_drift(std::span<const ClockSample> samples,
                                                      std::span<const std::int64_t> position_boot_us,
                                                      const Injection& injection, const Withholding& withholding,
                                                      const FitSettings& settings = {});

}  // namespace ics::timealign
