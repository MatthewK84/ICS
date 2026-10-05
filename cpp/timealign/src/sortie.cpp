#include "ics/timealign/sortie.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "ics/timealign/clock_fit.hpp"

namespace ics::timealign {

std::size_t SortieCounter::sortie(const std::int64_t boot_us) noexcept {
  // Boot times outside those ICS takes count as the nearest, so no
  // difference can overflow.
  const std::int64_t boot = std::clamp(boot_us, std::int64_t{0}, kMaxBootUs);
  const bool rebooted = latest_us_ && *latest_us_ - boot > reboot_gap_us_;
  sortie_ += rebooted ? 1U : 0U;
  latest_us_ = rebooted || !latest_us_ ? boot : std::max(*latest_us_, boot);
  return sortie_;
}

}  // namespace ics::timealign
