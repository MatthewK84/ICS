#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/frames/egm96.hpp"
#include "ics/frames/enu.hpp"
#include "ics/v1/common.pb.h"
#include "ics/v1/pli.pb.h"

namespace ics::flightlog {

// The onboard-log importer (ICS-025): an autopilot's estimator states, raw
// GNSS fixes and events, from a PX4 ULog or ArduPilot DataFlash log, as
// ics.v1.PliRecord and PliEvent (PLI_SOURCE_ULOG or PLI_SOURCE_DATAFLASH),
// every sample at the rate the log holds it.
//
// - States: one record for each estimated global position (PX4's
//   vehicle_global_position, ArduPilot's POS), with the latest estimated
//   velocity (vehicle_local_position; XKF1 or NKF1, core 0) and attitude
//   (vehicle_attitude; ATT) no older than max_age. Fix type FIX_TYPE_OTHER;
//   PX4's eph and epv are the sigmas.
// - GNSS: one record for each fix with a position (fix type 2D or better), with
//   the receiver's fix type, velocity and accuracies.
// - Events: arming, disarming and mode changes, and logged text, as status
//   text. PX4's modes are named as v1.17 names its navigation states.
// - Positions are WGS84 points; a height above mean sea level becomes one
//   above the ellipsoid through EGM96. Velocities, north-east-down at the
//   vehicle, are rotated into the range ENU frame. Attitudes are the body to
//   NED rotation, as MAVLink gives it.
// - entity_id is the vehicle's MAVLink system ID, from the log's parameters
//   (MAV_SYS_ID; MAV_SYSID or SYSID_THISMAV), so it matches the MAVLink
//   adapter's; the role comes from the settings.
//
// Time. Each GNSS sample with a time gives an offset from the autopilot's
// boot clock to UTC: PX4's time_utc_usec, the receiver's UTC, against its
// boot time; ArduPilot's GPS week and milliseconds, converted with the
// leap-second table (gps_time.hpp). Every record and event takes the latest
// offset at or before it, or the first offset for those before any, with
// PLI_TIME_BASIS_VEHICLE_GNSS, and keeps its boot time for the drift fit
// (ICS-026). A log without any GNSS time, such as one from PX4's SIH
// simulator, is untimed: its records and events have PLI_TIME_BASIS_UNSPECIFIED
// and valid_utc_ns 0, and only their boot times.

struct RoleAssignment {
  std::uint32_t system = 0;
  v1::EntityRole role = v1::ENTITY_ROLE_UNSPECIFIED;
};

struct ImportSettings {
  // Each vehicle's role, by MAVLink system ID; any other is ENTITY_ROLE_OTHER.
  std::vector<RoleAssignment> roles{};
  // The oldest velocity or attitude joined to a position.
  Duration max_age = std::chrono::seconds(1);
};

// A record or event, with the boot time the log gave it, in microseconds.
struct LogRecord {
  v1::PliRecord record;
  std::int64_t boot_us = 0;
};

struct LogEvent {
  v1::PliEvent event;
  std::int64_t boot_us = 0;
};

struct ImportCounts {
  // Estimated positions and GNSS fixes left out: no fix, a position not
  // valid, or not a geodetic point.
  std::size_t unplaced = 0;
  // GNSS samples whose time set the clock.
  std::size_t gnss_times = 0;
  // Of those, how many fell past the leap-second table (gps_time.hpp).
  std::size_t beyond_leap_table = 0;
};

struct LogContents {
  // The log's MAVLink system ID: 1, the autopilots' default, if it gives none.
  std::uint32_t system_id = 1;
  // Whether the log held a GNSS time; without one, everything is untimed.
  bool timed = false;
  std::vector<LogRecord> states;
  std::vector<LogRecord> gnss;
  // In boot time order.
  std::vector<LogEvent> events;
  ImportCounts counts;
};

// Imports a ULog log. Fails as ULog::parse does.
[[nodiscard]] Result<LogContents> import_ulog(std::span<const std::byte> log, const ImportSettings& settings,
                                              const frames::Egm96& geoid, const frames::EnuFrame& range);

// Imports a DataFlash log. Fails as DataFlash::parse does.
[[nodiscard]] Result<LogContents> import_dataflash(std::span<const std::byte> log, const ImportSettings& settings,
                                                   const frames::Egm96& geoid, const frames::EnuFrame& range);

// Imports either: a ULog log by its header, anything else as DataFlash.
[[nodiscard]] Result<LogContents> import_log(std::span<const std::byte> log, const ImportSettings& settings,
                                             const frames::Egm96& geoid, const frames::EnuFrame& range);

}  // namespace ics::flightlog
