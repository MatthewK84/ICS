// Fuzzes the PWI4 status parser (ICS-033) with any text, as the station
// network could deliver it. It may not overflow or crash, and a status it
// reads must hold only finite numbers.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string_view>

#include "ics/mount/pwi4_status.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  const std::string_view text(reinterpret_cast<const char*>(data), size);
  const auto status = ics::mount::parse_status(text);
  if (!status) {
    return 0;
  }
  for (const ics::mount::AxisStatus& axis : status->axes) {
    if (!std::isfinite(axis.position.value()) || !std::isfinite(axis.velocity_rad_per_s)) {
      std::abort();
    }
  }
  return 0;
}
