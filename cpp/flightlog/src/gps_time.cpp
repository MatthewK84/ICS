#include "ics/flightlog/gps_time.hpp"

#include <array>
#include <chrono>
#include <cstdint>

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"

namespace ics::flightlog {
namespace {

constexpr std::int64_t kGpsEpochUnix = 315'964'800;
constexpr std::int64_t kSecondsPerWeek = 604'800;
constexpr std::uint32_t kMsPerWeek = 604'800'000;
constexpr std::uint32_t kMsPerSecond = 1'000;
constexpr std::uint32_t kMaxWeek = 9'999;

// Each leap second since the GPS epoch: when it took effect, in Unix seconds,
// and GPS - UTC from then on (TAI - UTC - 19 s, from leap-seconds.list).
struct LeapSecond {
  std::int64_t effective_unix = 0;
  std::int64_t gps_minus_utc = 0;
};

constexpr std::array<LeapSecond, 18> kLeapSeconds{{
    {362'793'600, 1},     // 1981-07-01
    {394'329'600, 2},     // 1982-07-01
    {425'865'600, 3},     // 1983-07-01
    {489'024'000, 4},     // 1985-07-01
    {567'993'600, 5},     // 1988-01-01
    {631'152'000, 6},     // 1990-01-01
    {662'688'000, 7},     // 1991-01-01
    {709'948'800, 8},     // 1992-07-01
    {741'484'800, 9},     // 1993-07-01
    {773'020'800, 10},    // 1994-07-01
    {820'454'400, 11},    // 1996-01-01
    {867'715'200, 12},    // 1997-07-01
    {915'148'800, 13},    // 1999-01-01
    {1'136'073'600, 14},  // 2006-01-01
    {1'230'768'000, 15},  // 2009-01-01
    {1'341'100'800, 16},  // 2012-07-01
    {1'435'708'800, 17},  // 2015-07-01
    {1'483'228'800, 18},  // 2017-01-01
}};

// GPS - UTC at a GPS time given as Unix-epoch seconds on the GPS scale: the
// offset of the last leap second whose effective instant, on that scale, has
// passed.
[[nodiscard]] std::int64_t gps_minus_utc(const std::int64_t gps_unix) noexcept {
  std::int64_t offset = 0;
  for (const LeapSecond& leap : kLeapSeconds) {
    offset = gps_unix >= leap.effective_unix + leap.gps_minus_utc ? leap.gps_minus_utc : offset;
  }
  return offset;
}

}  // namespace

Result<GpsUtc> utc_from_gps(const std::uint32_t week, const std::uint32_t ms_of_week) noexcept {
  if (ms_of_week >= kMsPerWeek || week > kMaxWeek) {
    return fail(Error::kInvalidArgument);
  }
  const std::int64_t gps_unix = kGpsEpochUnix + (static_cast<std::int64_t>(week) * kSecondsPerWeek) + (ms_of_week / kMsPerSecond);
  const std::int64_t utc_unix = gps_unix - gps_minus_utc(gps_unix);
  const UtcTime utc = UtcTime(std::chrono::seconds(utc_unix)) + std::chrono::milliseconds(ms_of_week % kMsPerSecond);
  return GpsUtc{.utc = utc, .beyond_table = utc_unix >= kLeapSecondsKnownUntilUnix};
}

}  // namespace ics::flightlog
