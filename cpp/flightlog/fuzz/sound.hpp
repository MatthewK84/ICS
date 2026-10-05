#pragma once

// What the importer promises for any log (ICS-025), checked by both fuzz
// targets:
// - an import is timed exactly when a GNSS time set the clock;
// - every record carries the log's vehicle, a real geodetic point, finite
//   sigmas that are not negative, and a finite velocity and attitude when it
//   has them;
// - every boot time is one ICS takes, from 0 to 2100 as a Unix time, and
//   events come in boot time order;
// - an untimed record or event has no UTC time.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/flightlog/import.hpp"
#include "ics/frames/egm96.hpp"
#include "ics/frames/enu.hpp"
#include "ics/frames/geodetic.hpp"
#include "ics/v1/pli.pb.h"

namespace ics::flightlog::fuzz {

inline constexpr std::int64_t kMaxBootUs = 4'102'444'800'000'000;
inline constexpr std::uint32_t kTarget = 2;

inline void require(const bool condition) {
  if (!condition) {
    std::abort();
  }
}

// A small grid of constant height: the importer's use of the geoid is under
// test here, not EGM96 itself (frames/fuzz does that), and the fuzzer must
// not depend on the installed grid.
inline const frames::Egm96& geoid() {
  static const Result<frames::Egm96> grid = [] {
    constexpr int kCells = 8 * 5;
    std::string file = "P5\n# Offset -100\n# Scale 0.1\n8 5\n65535\n";
    for (int i = 0; i < kCells; ++i) {
      file.push_back(static_cast<char>(1000 >> 8U));
      file.push_back(static_cast<char>(1000 & 0xFFU));
    }
    return frames::Egm96::parse(file);
  }();
  require(grid.has_value());
  return *grid;
}

inline frames::EnuFrame range() {
  return frames::EnuFrame(frames::Geodetic::make(Degrees(40.0), Degrees(-100.0), Meters(700.0)).value());
}

inline ImportSettings settings() { return ImportSettings{.roles = {{.system = kTarget, .role = v1::ENTITY_ROLE_TARGET}}}; }

inline bool finite_sigma(const bool has, const double sigma) { return !has || (std::isfinite(sigma) && sigma >= 0.0); }

inline void require_time(const LogContents& contents, const std::int64_t boot_us, const v1::PliTimeBasis basis,
                         const std::int64_t utc_ns) {
  require(boot_us >= 0 && boot_us <= kMaxBootUs);
  require(basis == (contents.timed ? v1::PLI_TIME_BASIS_VEHICLE_GNSS : v1::PLI_TIME_BASIS_UNSPECIFIED));
  require(contents.timed || utc_ns == 0);
}

inline void require_vehicle(const LogContents& contents, const std::string& entity_id, const v1::PliSource source,
                            const v1::PliSource expected) {
  require(entity_id == std::to_string(contents.system_id));
  require(source == expected);
}

inline void require_sound(const LogContents& contents, const LogRecord& logged, const v1::PliSource source) {
  const v1::PliRecord& record = logged.record;
  require_vehicle(contents, record.entity_id(), record.source(), source);
  require(record.role() == (contents.system_id == kTarget ? v1::ENTITY_ROLE_TARGET : v1::ENTITY_ROLE_OTHER));
  require_time(contents, logged.boot_us, record.time_basis(), record.valid_utc_ns());
  const v1::GeodeticPoint& point = record.position();
  require(frames::Geodetic::make(Degrees(point.latitude_deg()), Degrees(point.longitude_deg()),
                                 Meters(point.height_ellipsoid_m()))
              .has_value());
  require(finite_sigma(record.has_horizontal_sigma_m(), record.horizontal_sigma_m()));
  require(finite_sigma(record.has_vertical_sigma_m(), record.vertical_sigma_m()));
  const v1::EnuVector& v = record.velocity_enu_mps();
  require(std::isfinite(v.east()) && std::isfinite(v.north()) && std::isfinite(v.up()));
  const v1::PliRecord::Attitude& q = record.attitude();
  require(std::isfinite(q.w()) && std::isfinite(q.x()) && std::isfinite(q.y()) && std::isfinite(q.z()));
}

inline void require_sound(const LogContents& contents, const v1::PliSource source) {
  require(contents.timed == (contents.counts.gnss_times > 0));
  require(contents.counts.beyond_leap_table <= contents.counts.gnss_times);
  require(contents.system_id >= 1 && contents.system_id <= 255);
  for (const LogRecord& state : contents.states) {
    require_sound(contents, state, source);
    require(state.record.fix_type() == v1::PliRecord::FIX_TYPE_OTHER);
  }
  for (const LogRecord& fix : contents.gnss) {
    require_sound(contents, fix, source);
  }
  require(std::ranges::is_sorted(contents.events, {}, &LogEvent::boot_us));
  for (const LogEvent& event : contents.events) {
    require_vehicle(contents, event.event.entity_id(), event.event.source(), source);
    require_time(contents, event.boot_us, event.event.time_basis(), event.event.time_utc_ns());
  }
}

}  // namespace ics::flightlog::fuzz
