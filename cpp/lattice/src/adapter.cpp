#include "ics/lattice/adapter.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <utility>

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/frames/enu.hpp"
#include "ics/frames/geodetic.hpp"
#include "ics/lattice/event.hpp"

namespace ics::lattice {
namespace {

// Whether the event reports where a live entity is now.
[[nodiscard]] bool current(const Event& event) noexcept {
  const bool reports = event.type == EventType::kPreexisting || event.type == EventType::kCreated ||
                       event.type == EventType::kUpdate;
  return reports && event.entity.live;
}

// The one-sigma semi-major axis of the east-north block: the square root of
// its larger eigenvalue. Nothing for a negative variance.
[[nodiscard]] std::optional<double> horizontal_sigma(const Covariance& covariance) noexcept {
  const double mean = (covariance.xx + covariance.yy) / 2.0;
  const double largest = mean + std::hypot((covariance.xx - covariance.yy) / 2.0, covariance.xy);
  if (covariance.xx < 0.0 || covariance.yy < 0.0 || !std::isfinite(largest)) {
    return std::nullopt;
  }
  return std::sqrt(largest);
}

void set_errors(v1::PliRecord& out, const Entity& entity) {
  if (!entity.position_covariance_m2) {
    return;
  }
  const Covariance& covariance = *entity.position_covariance_m2;
  const std::optional<double> horizontal = horizontal_sigma(covariance);
  if (horizontal) {
    out.set_horizontal_sigma_m(*horizontal);
  }
  if (entity.position->hae_m && covariance.zz >= 0.0) {
    out.set_vertical_sigma_m(std::sqrt(covariance.zz));
  }
}

// A velocity in the entity's ENU frame, in the range ENU frame.
void set_velocity(v1::EnuVector& out, const frames::Geodetic& where, const Enu& velocity,
                  const frames::EnuFrame& range) {
  const frames::EnuVector<MeterPerSecond> at_entity{MetersPerSecond(velocity.east), MetersPerSecond(velocity.north),
                                                    MetersPerSecond(velocity.up)};
  const frames::EnuVector<MeterPerSecond> in_range = range.rotate(frames::EnuFrame(where).rotate(at_entity));
  out.set_east(in_range.east.value());
  out.set_north(in_range.north.value());
  out.set_up(in_range.up.value());
}

}  // namespace

Adapter::Adapter(AdapterSettings settings, const frames::EnuFrame& range)
    : settings_(std::move(settings)), range_(range) {}

Result<Adapter> Adapter::make(AdapterSettings settings, const frames::EnuFrame& range) {
  if (settings.max_skew < Duration::zero()) {
    return fail(Error::kInvalidArgument);
  }
  return Adapter(std::move(settings), range);
}

std::optional<v1::PliRecord> Adapter::record(const Event& event, const UtcTime received) const {
  const Entity& entity = event.entity;
  if (!current(event) || !entity.position) {
    return std::nullopt;
  }
  const Position& position = *entity.position;
  const Result<frames::Geodetic> where = frames::Geodetic::make(
      Degrees(position.latitude_deg), Degrees(position.longitude_deg), Meters(position.hae_m.value_or(0.0)));
  if (!where) {
    return std::nullopt;
  }
  v1::PliRecord out;
  out.set_entity_id(entity.entity_id);
  out.set_role(role(entity.entity_id));
  out.set_source(v1::PLI_SOURCE_LATTICE);
  set_time(out, entity, received);
  out.mutable_position()->set_latitude_deg(where->latitude().value());
  out.mutable_position()->set_longitude_deg(where->longitude().value());
  out.mutable_position()->set_height_ellipsoid_m(where->height().value());
  if (entity.velocity_enu_mps) {
    set_velocity(*out.mutable_velocity_enu_mps(), *where, *entity.velocity_enu_mps, range_);
  }
  set_errors(out, entity);
  out.set_fix_type(v1::PliRecord::FIX_TYPE_OTHER);
  return out;
}

void Adapter::set_time(v1::PliRecord& out, const Entity& entity, const UtcTime received) const {
  const bool trusted = entity.source_update_time.has_value() &&
                       std::chrono::abs(*entity.source_update_time - received) <= settings_.max_skew;
  out.set_valid_utc_ns(to_utc_ns(trusted ? *entity.source_update_time : received));
  out.set_time_basis(trusted ? v1::PLI_TIME_BASIS_VEHICLE_GNSS : v1::PLI_TIME_BASIS_RECEIPT);
  out.set_received_utc_ns(to_utc_ns(received));
}

v1::EntityRole Adapter::role(const std::string& entity_id) const noexcept {
  const auto found = std::ranges::find(settings_.roles, entity_id, &RoleAssignment::entity_id);
  return found == settings_.roles.end() ? v1::ENTITY_ROLE_OTHER : found->role;
}

}  // namespace ics::lattice
