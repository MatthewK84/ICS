#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ics/common/units.hpp"
#include "ics/flightlog/import.hpp"
#include "ics/frames/egm96.hpp"
#include "ics/frames/enu.hpp"
#include "ics/frames/geodetic.hpp"
#include "ics/v1/pli.pb.h"

// The parts of the importer (import.hpp) that PX4 and ArduPilot logs share.
namespace ics::flightlog::detail {

// The latest boot time taken, in microseconds: 2100 as a Unix time, since
// some PX4 simulators count boot time from the Unix epoch. Later ones would
// overflow a time in nanoseconds.
inline constexpr std::int64_t kMaxBootUs = 4'102'444'800'000'000;

// GNSS times from 2100 on are refused, as by the MAVLink adapter: nobody's
// clock, and a boot time offset by a later one could overflow a time in
// nanoseconds.
inline constexpr std::int64_t kLatestUtcUs = 4'102'444'800'000'000;

// The autopilot's boot clock against UTC, from the log's GNSS times.
class BootClock {
 public:
  void add(std::int64_t boot_us, UtcTime utc);
  [[nodiscard]] bool empty() const noexcept { return offsets_.empty(); }
  // Every time added, in boot time order.
  [[nodiscard]] std::vector<GnssTime> times() const;
  // The UTC time of a boot time: by the latest offset at or before it, or by
  // the first offset. Nothing without any offset.
  [[nodiscard]] std::optional<UtcTime> utc(std::int64_t boot_us) const;

 private:
  // Boot times, and UTC minus boot time there, in boot time order.
  std::vector<std::pair<std::int64_t, Duration>> offsets_;
};

// What every record and event of one log shares.
struct Vehicle {
  std::string entity_id;
  v1::EntityRole role = v1::ENTITY_ROLE_OTHER;
  v1::PliSource source = v1::PLI_SOURCE_UNSPECIFIED;
};

[[nodiscard]] Vehicle vehicle(std::uint32_t system_id, const ImportSettings& settings, v1::PliSource source);

// What extracting records needs besides the log.
struct Context {
  const ImportSettings& settings;
  const frames::Egm96& geoid;
  const frames::EnuFrame& range;
  Vehicle vehicle;
};

struct Ned {
  double north = 0.0;
  double east = 0.0;
  double down = 0.0;
};

struct Quaternion {
  double w = 1.0;
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
};

// The most fields or columns read from one sample.
inline constexpr std::size_t kMaxValues = 4;

// A sample's boot time, in microseconds, and the values read from it, in the
// order asked for.
struct Reading {
  std::int64_t boot_us = 0;
  std::array<double, kMaxValues> values{};
};

// The latest of a series of samples at or before a time, no older than a
// limit. A log's times may go back, so the samples are kept in time order.
template <typename T>
class Latest {
 public:
  Latest(std::vector<std::pair<std::int64_t, T>> samples, const Duration max_age)
      : samples_(std::move(samples)),
        max_age_us_(std::chrono::duration_cast<std::chrono::microseconds>(max_age).count()) {
    std::ranges::stable_sort(samples_, {}, &std::pair<std::int64_t, T>::first);
  }

  [[nodiscard]] std::optional<T> at(const std::int64_t boot_us) const {
    const auto after = std::ranges::upper_bound(samples_, boot_us, {}, &std::pair<std::int64_t, T>::first);
    if (after == samples_.begin() || boot_us - std::prev(after)->first > max_age_us_) {
      return std::nullopt;
    }
    return std::prev(after)->second;
  }

 private:
  std::vector<std::pair<std::int64_t, T>> samples_;
  std::int64_t max_age_us_ = 0;
};

// A record with the vehicle's identity, a position and a fix type.
[[nodiscard]] v1::PliRecord record(const Context& context, const frames::Geodetic& where, v1::PliRecord::FixType fix);

// Sets what a record has of a velocity, NED at its position and rotated
// into the range frame, and an attitude, each when it is finite.
void set_motion(v1::PliRecord& out, const std::optional<Ned>& velocity, const std::optional<Quaternion>& attitude,
                const frames::Geodetic& where, const Context& context);

// Sets a sigma when it is a finite length.
void set_sigmas(v1::PliRecord& out, std::optional<double> horizontal, std::optional<double> vertical);

// A logged MAVLink system ID, when it is one (1 to 255); 1 otherwise, as
// autopilots default to.
[[nodiscard]] std::uint32_t system_id(std::optional<double> logged) noexcept;

[[nodiscard]] v1::PliEvent event(const Context& context, v1::PliEvent::Kind kind, std::string detail);

// Times every record and event by the clock, puts the events in boot time
// order, and keeps the clock's GNSS times.
void stamp(LogContents& contents, const BootClock& clock);

}  // namespace ics::flightlog::detail
