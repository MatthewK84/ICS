#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/timealign/clock_fit.hpp"
#include "ics/v1/pli.pb.h"

namespace ics::timealign {

// A record or event timed by its boot time through a sortie's model, with
// PLI_TIME_BASIS_VEHICLE_ALIGNED. Nothing when the model gives the boot time
// no UTC time ICS takes. A record keeps its received time.
[[nodiscard]] std::optional<v1::PliRecord> aligned(v1::PliRecord record, std::int64_t boot_us, const ClockModel& model);
[[nodiscard]] std::optional<v1::PliEvent> aligned(v1::PliEvent event, std::int64_t boot_us, const ClockModel& model);

// A vehicle's boot clock as the live adapters keep it, with no fit: UTC at a
// boot time is the boot time plus the offset of the latest clock pair at or
// before it, or of the first pair when it comes before them all. It times a
// sortie whose pairs do not lie on a straight line.
class SteppedClock {
 public:
  // Fails with Error::kInvalidArgument for a pair outside the times ICS takes,
  // and with Error::kEmpty when there is none.
  [[nodiscard]] static Result<SteppedClock> make(std::span<const ClockSample> samples);

  // Nothing unless the boot time and the UTC time it gives are ones ICS takes.
  [[nodiscard]] std::optional<UtcTime> utc(std::int64_t boot_us) const noexcept;

 private:
  explicit SteppedClock(std::vector<ClockSample> samples) noexcept;

  // In boot time order; pairs at the same boot time in the order given.
  std::vector<ClockSample> samples_;
};

// A record or event timed by its boot time through the latest clock pair, as
// the live adapters time them, with PLI_TIME_BASIS_VEHICLE_GNSS. Nothing when
// the clock gives the boot time no UTC time ICS takes.
[[nodiscard]] std::optional<v1::PliRecord> stepped(v1::PliRecord record, std::int64_t boot_us,
                                                   const SteppedClock& clock);
[[nodiscard]] std::optional<v1::PliEvent> stepped(v1::PliEvent event, std::int64_t boot_us, const SteppedClock& clock);

}  // namespace ics::timealign
