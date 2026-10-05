#include "ics/timealign/align.hpp"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "ics/common/check.hpp"
#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/timealign/clock_fit.hpp"
#include "ics/v1/pli.pb.h"

namespace ics::timealign {
namespace {

constexpr std::int64_t kNsPerUs = 1'000;

[[nodiscard]] bool in_range(const ClockSample& sample) noexcept {
  return sample.boot_us >= 0 && sample.boot_us <= kMaxBootUs && sample.utc_ns >= 0 && sample.utc_ns <= kMaxUtcNs;
}

[[nodiscard]] std::optional<v1::PliRecord> timed(v1::PliRecord record, const std::optional<UtcTime> utc,
                                                 const v1::PliTimeBasis basis) {
  if (!utc) {
    return std::nullopt;
  }
  record.set_valid_utc_ns(to_utc_ns(*utc));
  record.set_time_basis(basis);
  return record;
}

[[nodiscard]] std::optional<v1::PliEvent> timed(v1::PliEvent event, const std::optional<UtcTime> utc,
                                                const v1::PliTimeBasis basis) {
  if (!utc) {
    return std::nullopt;
  }
  event.set_time_utc_ns(to_utc_ns(*utc));
  event.set_time_basis(basis);
  return event;
}

}  // namespace

std::optional<v1::PliRecord> aligned(v1::PliRecord record, const std::int64_t boot_us, const ClockModel& model) {
  return timed(std::move(record), model.utc(boot_us), v1::PLI_TIME_BASIS_VEHICLE_ALIGNED);
}

std::optional<v1::PliEvent> aligned(v1::PliEvent event, const std::int64_t boot_us, const ClockModel& model) {
  return timed(std::move(event), model.utc(boot_us), v1::PLI_TIME_BASIS_VEHICLE_ALIGNED);
}

Result<SteppedClock> SteppedClock::make(const std::span<const ClockSample> samples) {
  if (!std::ranges::all_of(samples, in_range)) {
    return fail(Error::kInvalidArgument);
  }
  if (samples.empty()) {
    return fail(Error::kEmpty);
  }
  std::vector<ClockSample> sorted(samples.begin(), samples.end());
  std::ranges::stable_sort(sorted, {}, &ClockSample::boot_us);
  return SteppedClock(std::move(sorted));
}

SteppedClock::SteppedClock(std::vector<ClockSample> samples) noexcept : samples_(std::move(samples)) {}

std::optional<UtcTime> SteppedClock::utc(const std::int64_t boot_us) const noexcept {
  // make() keeps at least one pair, each in range, so the sum below fits.
  static_cast<void>(check(!samples_.empty()));
  if (std::clamp(boot_us, std::int64_t{0}, kMaxBootUs) != boot_us) {
    return std::nullopt;
  }
  const auto after = std::ranges::upper_bound(samples_, boot_us, {}, &ClockSample::boot_us);
  const ClockSample& latest = after == samples_.begin() ? samples_.front() : *std::prev(after);
  const std::int64_t utc_ns = latest.utc_ns + ((boot_us - latest.boot_us) * kNsPerUs);
  if (std::clamp(utc_ns, std::int64_t{0}, kMaxUtcNs) != utc_ns) {
    return std::nullopt;
  }
  return utc_from_ns(utc_ns);
}

std::optional<v1::PliRecord> stepped(v1::PliRecord record, const std::int64_t boot_us, const SteppedClock& clock) {
  return timed(std::move(record), clock.utc(boot_us), v1::PLI_TIME_BASIS_VEHICLE_GNSS);
}

std::optional<v1::PliEvent> stepped(v1::PliEvent event, const std::int64_t boot_us, const SteppedClock& clock) {
  return timed(std::move(event), clock.utc(boot_us), v1::PLI_TIME_BASIS_VEHICLE_GNSS);
}

}  // namespace ics::timealign
