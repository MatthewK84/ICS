#include "ics/mavlink/adapter.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "ics/common/check.hpp"
#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/frames/egm96.hpp"
#include "ics/frames/enu.hpp"
#include "ics/frames/geodetic.hpp"
#include "ics/mavlink/conversions.hpp"
#include "ics/mavlink/messages.hpp"
#include "ics/mavlink/modes.hpp"

namespace ics::mavlink {
namespace {

constexpr std::uint8_t kAutopilotComponent = 1;  // MAV_COMP_ID_AUTOPILOT1
constexpr std::uint8_t kAutopilotInvalid = 8;    // MAV_AUTOPILOT_INVALID: a ground station
constexpr unsigned kArmed = 0x80U;               // MAV_MODE_FLAG_SAFETY_ARMED
constexpr double kDegreesPerUnit = 1e-7;
constexpr double kMetersPerMillimeter = 1e-3;
constexpr double kMetersPerCentimeter = 1e-2;
// SYSTEM_TIME's UTC times from 2100 on are refused: they are nobody's clock,
// and much larger ones would overflow a time in nanoseconds.
constexpr std::uint64_t kLatestUnixMicroseconds = 4'102'444'800'000'000;

[[nodiscard]] v1::PliEvent event(const std::uint8_t system, const UtcTime time, const v1::PliEvent::Kind kind) {
  v1::PliEvent out;
  out.set_entity_id(std::to_string(system));
  out.set_source(v1::PLI_SOURCE_MAVLINK);
  out.set_time_utc_ns(to_utc_ns(time));
  out.set_time_basis(v1::PLI_TIME_BASIS_RECEIPT);
  out.set_kind(kind);
  return out;
}

template <typename T>
[[nodiscard]] bool fresh(const std::optional<T>& stamped, const UtcTime received, const Duration max_age) noexcept {
  return stamped.has_value() && received - stamped->at <= max_age;
}

// The velocity MAVLink gives in north-east-down axes at the vehicle, in the
// range ENU frame.
void set_velocity(v1::EnuVector& out, const frames::Geodetic& where, const GlobalPositionInt& message,
                  const frames::EnuFrame& range) {
  const frames::EnuFrame local(where);
  const frames::EnuVector<MeterPerSecond> at_vehicle{MetersPerSecond(message.vy * kMetersPerCentimeter),
                                                     MetersPerSecond(message.vx * kMetersPerCentimeter),
                                                     MetersPerSecond(-message.vz * kMetersPerCentimeter)};
  const frames::EnuVector<MeterPerSecond> in_range = range.rotate(local.rotate(at_vehicle));
  out.set_east(in_range.east.value());
  out.set_north(in_range.north.value());
  out.set_up(in_range.up.value());
}

}  // namespace

// Hands each message to the adapter.
struct Dispatch {
  Adapter& adapter;
  const Adapter::Context& context;

  void operator()(const Heartbeat& message) const { adapter.heartbeat(message, context); }
  void operator()(const SystemTime& message) const { adapter.system_time(message, context); }
  void operator()(const GlobalPositionInt& message) const { adapter.position(message, context); }

  void operator()(const GpsRawInt& message) const {
    if (context.from_autopilot) {
      adapter.vehicles_[context.system].gps = Adapter::Received<GpsRawInt>{message, context.received};
    }
  }

  void operator()(const AttitudeQuaternion& message) const {
    if (context.from_autopilot) {
      adapter.vehicles_[context.system].attitude = Adapter::Received<AttitudeQuaternion>{message, context.received};
    }
  }

  void operator()(const CommandLong& message) const {
    v1::PliEvent out = event(message.target_system, context.received, v1::PliEvent::KIND_COMMAND);
    out.set_command(message.command);
    const std::array<float, 7>& p = message.params;
    out.set_detail(std::format("params {} {} {} {} {} {} {}", p[0], p[1], p[2], p[3], p[4], p[5], p[6]));
    context.out.events.push_back(std::move(out));
  }

  void operator()(const CommandAck& message) const {
    v1::PliEvent out = event(context.system, context.received, v1::PliEvent::KIND_COMMAND_ACK);
    out.set_command(message.command);
    out.set_command_result(message.result);
    context.out.events.push_back(std::move(out));
  }

  void operator()(const StatusText& message) const {
    v1::PliEvent out = event(context.system, context.received, v1::PliEvent::KIND_STATUS_TEXT);
    out.set_detail(printable(message.view()));
    context.out.events.push_back(std::move(out));
  }
};

Adapter::Adapter(AdapterSettings settings, const frames::Egm96& geoid, const frames::EnuFrame& range)
    : settings_(std::move(settings)), geoid_(geoid), range_(range) {}

void Adapter::receive(const Frame& frame, const UtcTime received, Output& out) {
  const Result<Message> message = decode(frame);
  if (!message) {
    return;
  }
  const Context context{.system = frame.system,
                        .from_autopilot = frame.component == kAutopilotComponent,
                        .received = received,
                        .out = out};
  std::visit(Dispatch{*this, context}, *message);
}

void Adapter::tick(const UtcTime now, Output& out) {
  for (std::size_t system = 0; system < vehicles_.size(); ++system) {
    Vehicle& vehicle = vehicles_[system];
    const UtcTime silent_from = vehicle.last_heartbeat + settings_.link_timeout;
    if (vehicle.heard && !vehicle.link_lost && now > silent_from) {
      out.events.push_back(event(static_cast<std::uint8_t>(system), silent_from, v1::PliEvent::KIND_LINK_LOST));
      vehicle.link_lost = true;
    }
  }
}

void Adapter::heartbeat(const Heartbeat& message, const Context& context) {
  if (!context.from_autopilot || message.autopilot == kAutopilotInvalid) {
    return;
  }
  Vehicle& vehicle = vehicles_[context.system];
  if (vehicle.link_lost) {
    context.out.events.push_back(event(context.system, context.received, v1::PliEvent::KIND_LINK_RESTORED));
  }
  const bool armed = (message.base_mode & kArmed) != 0U;
  if (armed != vehicle.armed) {
    const v1::PliEvent::Kind kind = armed ? v1::PliEvent::KIND_ARMED : v1::PliEvent::KIND_DISARMED;
    context.out.events.push_back(event(context.system, context.received, kind));
  }
  if (!vehicle.heard || message.custom_mode != vehicle.custom_mode) {
    v1::PliEvent changed = event(context.system, context.received, v1::PliEvent::KIND_MODE_CHANGED);
    changed.set_detail(mode_name(message));
    context.out.events.push_back(std::move(changed));
  }
  vehicle.heard = true;
  vehicle.armed = armed;
  vehicle.link_lost = false;
  vehicle.custom_mode = message.custom_mode;
  vehicle.last_heartbeat = context.received;
}

void Adapter::system_time(const SystemTime& message, const Context& context) {
  const bool has_utc = message.time_unix_usec != 0 && message.time_unix_usec < kLatestUnixMicroseconds;
  if (!context.from_autopilot || !has_utc) {
    return;
  }
  const auto utc = std::chrono::microseconds(static_cast<std::int64_t>(message.time_unix_usec));
  vehicles_[context.system].boot_to_utc = Duration(utc) - Duration(std::chrono::milliseconds(message.time_boot_ms));
  context.out.clocks.push_back(SystemClock{.system = context.system,
                                           .time_boot_ms = message.time_boot_ms,
                                           .time_unix_usec = message.time_unix_usec,
                                           .received = context.received});
}

void Adapter::position(const GlobalPositionInt& message, const Context& context) {
  if (!context.from_autopilot) {
    return;
  }
  const Result<frames::Geodetic> where =
      geoid_.from_msl(Degrees(message.lat * kDegreesPerUnit), Degrees(message.lon * kDegreesPerUnit),
                      Meters(message.alt * kMetersPerMillimeter));
  if (!where) {
    ++counts_.bad_positions;
    return;
  }
  // An MSL height of at most 2^31 mm plus a geoid height is always finite.
  static_cast<void>(check(std::isfinite(where->height().value())));
  const Vehicle& vehicle = vehicles_[context.system];
  Position out{.record = {}, .time_boot_ms = message.time_boot_ms};
  v1::PliRecord& record = out.record;
  record.set_entity_id(std::to_string(context.system));
  record.set_role(role(context.system));
  record.set_source(v1::PLI_SOURCE_MAVLINK);
  const Duration boot = std::chrono::milliseconds(message.time_boot_ms);
  const bool aligned = vehicle.boot_to_utc.has_value();
  record.set_valid_utc_ns(aligned ? (boot + *vehicle.boot_to_utc).count() : to_utc_ns(context.received));
  record.set_time_basis(aligned ? v1::PLI_TIME_BASIS_VEHICLE_GNSS : v1::PLI_TIME_BASIS_RECEIPT);
  record.set_received_utc_ns(to_utc_ns(context.received));
  record.mutable_position()->set_latitude_deg(where->latitude().value());
  record.mutable_position()->set_longitude_deg(where->longitude().value());
  record.mutable_position()->set_height_ellipsoid_m(where->height().value());
  set_velocity(*record.mutable_velocity_enu_mps(), *where, message, range_);
  fill_fix(record, vehicle, context.received);
  context.out.positions.push_back(std::move(out));
}

void Adapter::fill_fix(v1::PliRecord& record, const Vehicle& vehicle, const UtcTime received) const {
  if (fresh(vehicle.gps, received, settings_.max_age)) {
    const GpsRawInt& gps = vehicle.gps->message;
    record.set_fix_type(fix_type(gps.fix_type));
    if (gps.h_acc != 0) {
      record.set_horizontal_sigma_m(gps.h_acc * kMetersPerMillimeter);
    }
    if (gps.v_acc != 0) {
      record.set_vertical_sigma_m(gps.v_acc * kMetersPerMillimeter);
    }
  }
  if (fresh(vehicle.attitude, received, settings_.max_age)) {
    const AttitudeQuaternion& attitude = vehicle.attitude->message;
    v1::PliRecord::Attitude& out = *record.mutable_attitude();
    out.set_w(attitude.q1);
    out.set_x(attitude.q2);
    out.set_y(attitude.q3);
    out.set_z(attitude.q4);
  }
}

v1::EntityRole Adapter::role(const std::uint8_t system) const noexcept {
  const auto found = std::ranges::find_if(settings_.roles,
                                          [system](const RoleAssignment& assigned) { return assigned.system == system; });
  return found == settings_.roles.end() ? v1::ENTITY_ROLE_OTHER : found->role;
}

}  // namespace ics::mavlink
