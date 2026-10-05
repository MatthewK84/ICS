#include "ics/timealign/align.hpp"

#include <cstdint>
#include <optional>

#include "ics/common/units.hpp"
#include "ics/timealign/clock_fit.hpp"
#include "ics/v1/pli.pb.h"

namespace ics::timealign {

std::optional<v1::PliRecord> aligned(v1::PliRecord record, const std::int64_t boot_us, const ClockModel& model) {
  const std::optional<UtcTime> utc = model.utc(boot_us);
  if (!utc) {
    return std::nullopt;
  }
  record.set_valid_utc_ns(to_utc_ns(*utc));
  record.set_time_basis(v1::PLI_TIME_BASIS_VEHICLE_ALIGNED);
  return record;
}

std::optional<v1::PliEvent> aligned(v1::PliEvent event, const std::int64_t boot_us, const ClockModel& model) {
  const std::optional<UtcTime> utc = model.utc(boot_us);
  if (!utc) {
    return std::nullopt;
  }
  event.set_time_utc_ns(to_utc_ns(*utc));
  event.set_time_basis(v1::PLI_TIME_BASIS_VEHICLE_ALIGNED);
  return event;
}

}  // namespace ics::timealign
