#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"

namespace ics::timealign {

// Time alignment (ICS-026): a vehicle's boot clock against UTC, fitted per
// sortie as an offset and a constant drift from the pairs the vehicle reports
// (MAVLink SYSTEM_TIME; the GNSS times of its onboard log). The fit then times
// every record by its boot time (align.hpp). See docs/frames-and-time.md.

// The latest boot time and UTC time ICS takes: 2100 as a Unix time.
inline constexpr std::int64_t kMaxBootUs = 4'102'444'800'000'000;
inline constexpr std::int64_t kMaxUtcNs = 4'102'444'800'000'000'000;

// A fit's residuals, in nanoseconds, kept as doubles so no residual of a wild
// sample can overflow.
using Nanoseconds = std::chrono::duration<double, std::nano>;

// One pair a vehicle reports: its boot time, in microseconds, and the UTC time
// it gave for the same instant.
struct ClockSample {
  std::int64_t boot_us = 0;
  std::int64_t utc_ns = 0;
};

struct FitSettings {
  // A sample is an outlier when its residual is beyond this many standard
  // deviations, estimated from the residuals' median absolute deviation, and
  // beyond reject_floor.
  double reject_mads = 3.5;
  Duration reject_floor = std::chrono::milliseconds(2);
  // Rounds of rejecting outliers and fitting again.
  std::size_t max_iterations = 5;
  // The fewest samples a fit takes.
  std::size_t min_samples = 3;
};

// UTC as a straight line of boot time: from an origin, each microsecond of
// boot time is (1 + drift) microseconds of UTC.
class ClockModel {
 public:
  ClockModel(std::int64_t origin_boot_us, std::int64_t origin_utc_ns, double drift) noexcept
      : origin_boot_us_(origin_boot_us), origin_utc_ns_(origin_utc_ns), drift_(drift) {}

  // The UTC time of a boot time; nothing unless both are times ICS takes.
  [[nodiscard]] std::optional<UtcTime> utc(std::int64_t boot_us) const noexcept;

  // How much faster UTC runs than the boot clock, in parts per million.
  [[nodiscard]] double drift_ppm() const noexcept { return drift_ * 1e6; }
  [[nodiscard]] std::int64_t origin_boot_us() const noexcept { return origin_boot_us_; }
  [[nodiscard]] UtcTime origin_utc() const noexcept { return utc_from_ns(origin_utc_ns_); }

 private:
  std::int64_t origin_boot_us_ = 0;
  std::int64_t origin_utc_ns_ = 0;
  double drift_ = 0.0;
};

// A model fitted to samples, and how well it fits them.
struct ClockFit {
  ClockModel model;
  // Samples the fit used, and outliers it left out.
  std::size_t used = 0;
  std::size_t rejected = 0;
  // The used samples' residuals: their UTC minus the model's.
  Nanoseconds residual_rms{};
  Nanoseconds residual_max{};
  // The used samples' boot time span.
  std::int64_t first_boot_us = 0;
  std::int64_t last_boot_us = 0;
};

// Fits a model to one sortie's samples by least squares, then leaves out the
// outliers and fits again, until none is left or after max_iterations rounds.
// Fails with Error::kInvalidArgument for a sample outside the times ICS takes
// (a boot time or UTC time before 1970 or from 2100), and with Error::kEmpty
// when fewer than min_samples remain or their boot times are all the same.
[[nodiscard]] Result<ClockFit> fit_clock(std::span<const ClockSample> samples, const FitSettings& settings = {});

// The most a sortie's clock pairs may stray from their fitted line, as an
// RMS, for the line to time the sortie. ArduCopter SITL's stray 0.13 ms, and
// a clock read in whole milliseconds about 0.3 ms. PX4 SIH's have strayed
// 0.8 ms to 13 ms: its boot clock is simulated time, whose rate wanders
// against the host clock that gives its UTC.
inline constexpr Nanoseconds kStraightRms = std::chrono::microseconds(500);

// Whether a fit's clock pairs lie on a straight line: their residuals' RMS is
// within the bound. A fit that is not straight should time nothing, and the
// adapter's live times, set from each pair as it comes, stand instead.
[[nodiscard]] bool straight(const ClockFit& fit, Nanoseconds bound = kStraightRms) noexcept;

}  // namespace ics::timealign
