#include "importing.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ics/common/units.hpp"
#include "ics/flightlog/import.hpp"
#include "ics/frames/enu.hpp"
#include "ics/frames/geodetic.hpp"
#include "ics/v1/pli.pb.h"

namespace ics::flightlog::detail {
namespace {

constexpr double kMaxSystemId = 255.0;

struct MeterPerSecond;
using MetersPerSecond = Quantity<MeterPerSecond>;

[[nodiscard]] UtcTime from_boot(const std::int64_t boot_us, const Duration offset) noexcept {
  return UtcTime(std::chrono::microseconds(boot_us)) + offset;
}

[[nodiscard]] bool is_length(const std::optional<double>& value) noexcept {
  return value.has_value() && std::isfinite(*value) && *value >= 0.0;
}

[[nodiscard]] bool is_finite(const std::optional<Ned>& v) noexcept {
  return v.has_value() && std::isfinite(v->north + v->east + v->down);
}

[[nodiscard]] bool is_finite(const std::optional<Quaternion>& q) noexcept {
  return q.has_value() && std::isfinite(q->w + q->x + q->y + q->z);
}

void set_velocity(v1::PliRecord& out, const Ned& velocity, const frames::Geodetic& where, const frames::EnuFrame& range) {
  const frames::EnuVector<MeterPerSecond> at_vehicle{MetersPerSecond(velocity.east), MetersPerSecond(velocity.north),
                                                     MetersPerSecond(-velocity.down)};
  const frames::EnuVector<MeterPerSecond> in_range =
      range.rotate(frames::EnuFrame(where).rotate(at_vehicle));
  out.mutable_velocity_enu_mps()->set_east(in_range.east.value());
  out.mutable_velocity_enu_mps()->set_north(in_range.north.value());
  out.mutable_velocity_enu_mps()->set_up(in_range.up.value());
}

void set_attitude(v1::PliRecord& out, const Quaternion& attitude) {
  out.mutable_attitude()->set_w(attitude.w);
  out.mutable_attitude()->set_x(attitude.x);
  out.mutable_attitude()->set_y(attitude.y);
  out.mutable_attitude()->set_z(attitude.z);
}

}  // namespace

void BootClock::add(const std::int64_t boot_us, const UtcTime utc) {
  const Duration offset = utc - UtcTime(std::chrono::microseconds(boot_us));
  const auto after = std::ranges::upper_bound(offsets_, boot_us, {}, &std::pair<std::int64_t, Duration>::first);
  offsets_.insert(after, {boot_us, offset});
}

std::vector<GnssTime> BootClock::times() const {
  std::vector<GnssTime> out;
  out.reserve(offsets_.size());
  std::ranges::transform(offsets_, std::back_inserter(out), [](const std::pair<std::int64_t, Duration>& offset) {
    return GnssTime{.boot_us = offset.first, .utc = from_boot(offset.first, offset.second)};
  });
  return out;
}

std::optional<UtcTime> BootClock::utc(const std::int64_t boot_us) const {
  if (offsets_.empty()) {
    return std::nullopt;
  }
  const auto after = std::ranges::upper_bound(offsets_, boot_us, {}, &std::pair<std::int64_t, Duration>::first);
  const auto& offset = after == offsets_.begin() ? *after : *std::prev(after);
  return from_boot(boot_us, offset.second);
}

Vehicle vehicle(const std::uint32_t system_id, const ImportSettings& settings, const v1::PliSource source) {
  const auto found = std::ranges::find(settings.roles, system_id, &RoleAssignment::system);
  return Vehicle{.entity_id = std::to_string(system_id),
                 .role = found == settings.roles.end() ? v1::ENTITY_ROLE_OTHER : found->role,
                 .source = source};
}

v1::PliRecord record(const Context& context, const frames::Geodetic& where, const v1::PliRecord::FixType fix) {
  v1::PliRecord out;
  out.set_entity_id(context.vehicle.entity_id);
  out.set_role(context.vehicle.role);
  out.set_source(context.vehicle.source);
  out.mutable_position()->set_latitude_deg(where.latitude().value());
  out.mutable_position()->set_longitude_deg(where.longitude().value());
  out.mutable_position()->set_height_ellipsoid_m(where.height().value());
  out.set_fix_type(fix);
  return out;
}

void set_sigmas(v1::PliRecord& out, const std::optional<double> horizontal, const std::optional<double> vertical) {
  if (is_length(horizontal)) {
    out.set_horizontal_sigma_m(*horizontal);
  }
  if (is_length(vertical)) {
    out.set_vertical_sigma_m(*vertical);
  }
}

void set_motion(v1::PliRecord& out, const std::optional<Ned>& velocity, const std::optional<Quaternion>& attitude,
                const frames::Geodetic& where, const Context& context) {
  if (is_finite(velocity)) {
    set_velocity(out, *velocity, where, context.range);
  }
  if (is_finite(attitude)) {
    set_attitude(out, *attitude);
  }
}

std::uint32_t system_id(const std::optional<double> logged) noexcept {
  const bool valid = logged.has_value() && *logged >= 1.0 && *logged <= kMaxSystemId;
  return valid ? static_cast<std::uint32_t>(*logged) : 1U;
}

v1::PliEvent event(const Context& context, const v1::PliEvent::Kind kind, std::string detail) {
  v1::PliEvent out;
  out.set_entity_id(context.vehicle.entity_id);
  out.set_source(context.vehicle.source);
  out.set_kind(kind);
  out.set_detail(std::move(detail));
  return out;
}

void stamp(LogContents& contents, const BootClock& clock) {
  contents.timed = !clock.empty();
  contents.gnss_times = clock.times();
  const v1::PliTimeBasis basis = contents.timed ? v1::PLI_TIME_BASIS_VEHICLE_GNSS : v1::PLI_TIME_BASIS_UNSPECIFIED;
  const auto utc_ns = [&clock](const std::int64_t boot_us) {
    const std::optional<UtcTime> utc = clock.utc(boot_us);
    return utc ? to_utc_ns(*utc) : 0;
  };
  const auto stamp_records = [&utc_ns, basis](std::vector<LogRecord>& records) {
    for (LogRecord& record : records) {
      record.record.set_valid_utc_ns(utc_ns(record.boot_us));
      record.record.set_time_basis(basis);
    }
  };
  stamp_records(contents.states);
  stamp_records(contents.gnss);
  std::ranges::stable_sort(contents.events, {}, &LogEvent::boot_us);
  for (LogEvent& logged : contents.events) {
    logged.event.set_time_utc_ns(utc_ns(logged.boot_us));
    logged.event.set_time_basis(basis);
  }
}

}  // namespace ics::flightlog::detail
