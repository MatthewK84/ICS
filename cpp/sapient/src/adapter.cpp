#include "ics/sapient/adapter.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "ics/common/check.hpp"
#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/frames/egm96.hpp"
#include "ics/frames/enu.hpp"
#include "ics/frames/geodetic.hpp"
#include "ics/sapient/location.hpp"
#include "ics/sapient/registration.hpp"

namespace ics::sapient {
namespace {

namespace bsi = sapient_msg::bsi_flex_335_v2_0;

// The latest timestamp taken, 2200-12-31T23:59:59Z, as for Lattice (ICS-023):
// nanoseconds since 1970 fit an int64 until 2262.
constexpr std::int64_t kLastSecond = 7'289'654'399;
constexpr std::int32_t kLastNano = 999'999'999;

// The message's timestamp, if it has a usable one.
[[nodiscard]] std::optional<UtcTime> sent(const Message& message) noexcept {
  const std::int64_t seconds = message.timestamp().seconds();
  const std::int32_t nanos = message.timestamp().nanos();
  if (!message.has_timestamp() || seconds < 0 || seconds > kLastSecond || nanos < 0 || nanos > kLastNano) {
    return std::nullopt;
  }
  return UtcTime(std::chrono::seconds(seconds) + std::chrono::nanoseconds(nanos));
}

[[nodiscard]] bool finite(const frames::EnuVector<MeterPerSecond>& velocity) noexcept {
  return std::isfinite(velocity.east.value()) && std::isfinite(velocity.north.value()) &&
         std::isfinite(velocity.up.value());
}

}  // namespace

Adapter::Adapter(AdapterSettings settings, const frames::Egm96& geoid, const frames::EnuFrame& range)
    : settings_(std::move(settings)), geoid_(geoid), range_(range) {}

Result<Adapter> Adapter::make(AdapterSettings settings, const frames::Egm96& geoid, const frames::EnuFrame& range) {
  if (settings.max_skew < Duration::zero()) {
    return fail(Error::kInvalidArgument);
  }
  return Adapter(std::move(settings), geoid, range);
}

std::optional<v1::PliRecord> Adapter::receive(const Message& message, const UtcTime received) {
  if (message.has_registration()) {
    registration(message);
  }
  if (message.has_detection_report()) {
    return detection(message, received);
  }
  return std::nullopt;
}

void Adapter::registration(const Message& message) {
  const std::string& node_id = message.node_id();
  const bool known = nodes_.contains(node_id);
  const bool usable_id = !node_id.empty() && node_id.size() <= kMaxNodeIdBytes;
  if (!usable_id || (!known && nodes_.size() >= settings_.max_nodes)) {
    ++counts_.registrations_ignored;
    return;
  }
  nodes_.insert_or_assign(node_id, read_units(message.registration()));
  static_cast<void>(check(nodes_.size() <= settings_.max_nodes));
}

std::optional<v1::PliRecord> Adapter::detection(const Message& message, const UtcTime received) {
  const bsi::DetectionReport& report = message.detection_report();
  if (report.has_range_bearing()) {
    ++counts_.range_bearing;
    return std::nullopt;
  }
  if (report.object_id().empty()) {
    ++counts_.unidentified;
    return std::nullopt;
  }
  const Result<Fix> fix = report.has_location()
                              ? to_fix(report.location(), units(message.node_id()).zone, geoid_.get())
                              : Result<Fix>(fail(Error::kInvalidArgument));
  if (!fix) {
    ++counts_.unlocated;
    return std::nullopt;
  }
  v1::PliRecord out;
  out.set_entity_id(report.object_id());
  out.set_role(role(report.object_id(), report.id()));
  out.set_source(v1::PLI_SOURCE_SAPIENT);
  set_time(out, message, received);
  out.mutable_position()->set_latitude_deg(fix->point.latitude().value());
  out.mutable_position()->set_longitude_deg(fix->point.longitude().value());
  out.mutable_position()->set_height_ellipsoid_m(fix->point.height().value());
  set_velocity(out, fix->point, message);
  if (fix->horizontal_sigma_m) {
    out.set_horizontal_sigma_m(*fix->horizontal_sigma_m);
  }
  if (fix->vertical_sigma_m) {
    out.set_vertical_sigma_m(*fix->vertical_sigma_m);
  }
  out.set_fix_type(v1::PliRecord::FIX_TYPE_OTHER);
  return out;
}

void Adapter::set_time(v1::PliRecord& out, const Message& message, const UtcTime received) const {
  const std::optional<UtcTime> at = sent(message);
  const bool trusted = at.has_value() && std::chrono::abs(*at - received) <= settings_.max_skew;
  out.set_valid_utc_ns(to_utc_ns(trusted ? *at : received));
  out.set_time_basis(trusted ? v1::PLI_TIME_BASIS_VEHICLE_GNSS : v1::PLI_TIME_BASIS_RECEIPT);
  out.set_received_utc_ns(to_utc_ns(received));
}

// The velocity in the object's ENU frame, in its node's units, rotated into
// the range ENU frame.
void Adapter::set_velocity(v1::PliRecord& out, const frames::Geodetic& where, const Message& message) const {
  const bsi::DetectionReport& report = message.detection_report();
  const std::optional<VelocityUnits> velocity_units = units(message.node_id()).velocity;
  if (!report.has_enu_velocity() || !velocity_units) {
    return;
  }
  const bsi::ENUVelocity& velocity = report.enu_velocity();
  const frames::EnuVector<MeterPerSecond> at_object{
      MetersPerSecond(velocity.east_rate() * velocity_units->horizontal_mps),
      MetersPerSecond(velocity.north_rate() * velocity_units->horizontal_mps),
      MetersPerSecond(velocity.up_rate() * velocity_units->vertical_mps)};
  if (!velocity.has_east_rate() || !velocity.has_north_rate() || !finite(at_object)) {
    return;
  }
  const frames::EnuVector<MeterPerSecond> in_range = range_.rotate(frames::EnuFrame(where).rotate(at_object));
  out.mutable_velocity_enu_mps()->set_east(in_range.east.value());
  out.mutable_velocity_enu_mps()->set_north(in_range.north.value());
  out.mutable_velocity_enu_mps()->set_up(in_range.up.value());
}

NodeUnits Adapter::units(const std::string& node_id) const {
  const auto found = nodes_.find(node_id);
  return found == nodes_.end() ? NodeUnits{} : found->second;
}

v1::EntityRole Adapter::role(const std::string& object_id, const std::string& id) const noexcept {
  const auto found = std::ranges::find_if(settings_.roles, [&object_id, &id](const RoleAssignment& assignment) {
    return assignment.id == object_id || (!id.empty() && assignment.id == id);
  });
  return found == settings_.roles.end() ? v1::ENTITY_ROLE_OTHER : found->role;
}

}  // namespace ics::sapient
