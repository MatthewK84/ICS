#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "ics/common/units.hpp"
#include "ics/frames/egm96.hpp"
#include "ics/frames/enu.hpp"
#include "ics/mavlink/frame.hpp"
#include "ics/mavlink/messages.hpp"
#include "ics/v1/common.pb.h"
#include "ics/v1/pli.pb.h"

namespace ics::mavlink {

// The MAVLink adapter (ICS-021): turns the frames a TAP port carries into
// ics.v1.PliRecord and PliEvent.
//
// Records. One PliRecord for each GLOBAL_POSITION_INT from a vehicle's
// autopilot (component 1), with:
// - the height above the ellipsoid, from MAVLink's height above mean sea
//   level and the EGM96 geoid;
// - the velocity rotated from the vehicle's north-east-down axes into the
//   range ENU frame;
// - the fix type and the one-sigma accuracies of the vehicle's last
//   GPS_RAW_INT, and the attitude of its last ATTITUDE_QUATERNION, each only
//   if received within max_age of the position.
//
// Time. Each SYSTEM_TIME with a UTC time gives the vehicle's boot-to-UTC
// offset, and a record's valid time is its time_boot_ms plus the last offset
// (PLI_TIME_BASIS_VEHICLE_GNSS). Until a vehicle sends one, its records take
// the time they were received (PLI_TIME_BASIS_RECEIPT). Events always take
// the time they were received: MAVLink stamps none of them. Each such
// SYSTEM_TIME is also passed on (Output::clocks), so the time alignment
// (ICS-026) can fit the vehicle's clock over the whole sortie.
//
// Events, each for the system it concerns:
// - ARMED and DISARMED when the armed flag in a vehicle's HEARTBEAT changes,
//   counting a vehicle as disarmed before its first;
// - MODE_CHANGED when its custom_mode changes, and on its first HEARTBEAT,
//   with the mode's name (modes.hpp);
// - COMMAND for each COMMAND_LONG, for its target system;
// - COMMAND_ACK for each COMMAND_ACK from a vehicle;
// - STATUS_TEXT for each STATUSTEXT, with any character outside printable
//   ASCII replaced by '?';
// - LINK_LOST when a vehicle's heartbeats stop for longer than link_timeout,
//   at the last heartbeat plus the timeout, and LINK_RESTORED at the next.
//
// A HEARTBEAT counts only from a vehicle's autopilot, not from a ground
// station (MAV_AUTOPILOT_INVALID).

// Metres per second, for velocities.
struct MeterPerSecond;
using MetersPerSecond = Quantity<MeterPerSecond>;

struct RoleAssignment {
  std::uint8_t system = 0;
  v1::EntityRole role = v1::ENTITY_ROLE_UNSPECIFIED;
};

struct AdapterSettings {
  // Each vehicle's role in the engagement; a system not listed is
  // ENTITY_ROLE_OTHER.
  std::vector<RoleAssignment> roles;
  Duration link_timeout = std::chrono::seconds(3);
  Duration max_age = std::chrono::seconds(1);
};

// A record, with the boot time MAVLink gave its position, so a replay can be
// matched against the vehicle's own log.
struct Position {
  v1::PliRecord record;
  std::uint32_t time_boot_ms = 0;
};

// A vehicle's SYSTEM_TIME with a UTC time: its boot time and its UTC time for
// the same instant, which the time alignment (ICS-026) fits.
struct SystemClock {
  std::uint8_t system = 0;
  std::uint32_t time_boot_ms = 0;
  std::uint64_t time_unix_usec = 0;
  UtcTime received{};
};

// What the adapter produced from one call. The caller empties it.
struct Output {
  std::vector<Position> positions;
  std::vector<v1::PliEvent> events;
  std::vector<SystemClock> clocks;
};

struct AdapterCounts {
  // GLOBAL_POSITION_INT messages dropped for a latitude or longitude out of
  // range.
  std::size_t bad_positions = 0;
};

class Adapter {
 public:
  // The geoid is borrowed and must outlive the adapter.
  Adapter(AdapterSettings settings, const frames::Egm96& geoid, const frames::EnuFrame& range);

  // Takes one frame, received at the time given.
  void receive(const Frame& frame, UtcTime received, Output& out);

  // Reports the links that have been silent longer than the timeout at now.
  void tick(UtcTime now, Output& out);

  [[nodiscard]] const AdapterCounts& counts() const noexcept { return counts_; }

 private:
  template <typename T>
  struct Received {
    T message;
    UtcTime at;
  };

  struct Vehicle {
    bool heard = false;
    bool armed = false;
    bool link_lost = false;
    std::uint32_t custom_mode = 0;
    UtcTime last_heartbeat{};
    std::optional<Duration> boot_to_utc;
    std::optional<Received<GpsRawInt>> gps;
    std::optional<Received<AttitudeQuaternion>> attitude;
  };

  struct Context {
    std::uint8_t system = 0;
    bool from_autopilot = false;
    UtcTime received;
    Output& out;
  };

  friend struct Dispatch;

  void heartbeat(const Heartbeat& message, const Context& context);
  void system_time(const SystemTime& message, const Context& context);
  void position(const GlobalPositionInt& message, const Context& context);
  void fill_fix(v1::PliRecord& record, const Vehicle& vehicle, UtcTime received) const;
  [[nodiscard]] v1::EntityRole role(std::uint8_t system) const noexcept;

  AdapterSettings settings_;
  const frames::Egm96& geoid_;
  frames::EnuFrame range_;
  std::array<Vehicle, 256> vehicles_{};
  AdapterCounts counts_;
};

}  // namespace ics::mavlink
