#pragma once

#include <cstdint>
#include <optional>

#include "ics/timealign/clock_fit.hpp"
#include "ics/v1/pli.pb.h"

namespace ics::timealign {

// A record or event timed by its boot time through a sortie's model, with
// PLI_TIME_BASIS_VEHICLE_ALIGNED. Nothing when the model gives the boot time
// no UTC time ICS takes. A record keeps its received time.
[[nodiscard]] std::optional<v1::PliRecord> aligned(v1::PliRecord record, std::int64_t boot_us, const ClockModel& model);
[[nodiscard]] std::optional<v1::PliEvent> aligned(v1::PliEvent event, std::int64_t boot_us, const ClockModel& model);

}  // namespace ics::timealign
