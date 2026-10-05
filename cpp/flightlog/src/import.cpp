#include "ics/flightlog/import.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "ics/common/error.hpp"
#include "ics/frames/egm96.hpp"
#include "ics/frames/enu.hpp"

namespace ics::flightlog {

Result<LogContents> import_log(const std::span<const std::byte> log, const ImportSettings& settings,
                               const frames::Egm96& geoid, const frames::EnuFrame& range) {
  constexpr std::array<std::uint8_t, 7> kULogMagic{0x55, 0x4c, 0x6f, 0x67, 0x01, 0x12, 0x35};
  const bool ulog = log.size() >= kULogMagic.size() &&
                    std::ranges::equal(log.first(kULogMagic.size()), kULogMagic, [](const std::byte b, const std::uint8_t m) {
                      return std::to_integer<std::uint8_t>(b) == m;
                    });
  return ulog ? import_ulog(log, settings, geoid, range) : import_dataflash(log, settings, geoid, range);
}

}  // namespace ics::flightlog
