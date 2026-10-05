#include "ics/mavlink/conversions.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>

namespace ics::mavlink {
namespace {

constexpr char kFirstPrintable = 0x20;
constexpr char kLastPrintable = 0x7E;

}  // namespace

v1::PliRecord::FixType fix_type(const std::uint8_t gps_fix_type) noexcept {
  switch (gps_fix_type) {
    case 0:  // GPS_FIX_TYPE_NO_GPS
    case 1:  // GPS_FIX_TYPE_NO_FIX
      return v1::PliRecord::FIX_TYPE_NONE;
    case 2:
      return v1::PliRecord::FIX_TYPE_TWO_DIMENSIONAL;
    case 3:
    case 8:  // GPS_FIX_TYPE_PPP
      return v1::PliRecord::FIX_TYPE_THREE_DIMENSIONAL;
    case 4:
      return v1::PliRecord::FIX_TYPE_DGNSS;
    case 5:
      return v1::PliRecord::FIX_TYPE_RTK_FLOAT;
    case 6:
      return v1::PliRecord::FIX_TYPE_RTK_FIXED;
    default:  // GPS_FIX_TYPE_STATIC, and any value MAVLink adds
      return v1::PliRecord::FIX_TYPE_OTHER;
  }
}

std::string printable(const std::string_view text) {
  std::string out(text);
  std::ranges::replace_if(out, [](const char c) { return c < kFirstPrintable || c > kLastPrintable; }, '?');
  return out;
}

}  // namespace ics::mavlink
