#include "ics/cot/adapter.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/cot/event.hpp"
#include "ics/cot/uncertainty.hpp"
#include "ics/frames/enu.hpp"
#include "ics/frames/geodetic.hpp"

namespace ics::cot {
namespace {

// Every atom's type starts with "a-"; a machine GPS position's how with "m-g".
constexpr std::string_view kAtom = "a-";
constexpr std::string_view kMachineGps = "m-g";

[[nodiscard]] bool hae_known(const Point& point) noexcept { return point.hae_m < kUnknown; }

[[nodiscard]] v1::PliRecord::FixType fix_type(const Event& event) noexcept {
  if (!event.how.starts_with(kMachineGps)) {
    return v1::PliRecord::FIX_TYPE_OTHER;
  }
  return hae_known(event.point) ? v1::PliRecord::FIX_TYPE_THREE_DIMENSIONAL : v1::PliRecord::FIX_TYPE_TWO_DIMENSIONAL;
}

// A track's horizontal velocity, east and north at the vehicle, in the range
// ENU frame.
void set_velocity(v1::EnuVector& out, const frames::Geodetic& where, const Track& track,
                  const frames::EnuFrame& range) {
  const double course = to_radians(Degrees(track.course_deg)).value();
  const frames::EnuVector<MeterPerSecond> at_vehicle{MetersPerSecond(track.speed_mps * std::sin(course)),
                                                     MetersPerSecond(track.speed_mps * std::cos(course)),
                                                     MetersPerSecond(0.0)};
  const frames::EnuVector<MeterPerSecond> in_range = range.rotate(frames::EnuFrame(where).rotate(at_vehicle));
  out.set_east(in_range.east.value());
  out.set_north(in_range.north.value());
  out.set_up(in_range.up.value());
}

}  // namespace

Adapter::Adapter(AdapterSettings settings, const SigmaFactors factors, const frames::EnuFrame& range)
    : settings_(std::move(settings)), factors_(factors), range_(range) {}

Result<Adapter> Adapter::make(AdapterSettings settings, const frames::EnuFrame& range) {
  const Result<SigmaFactors> factors = sigma_factors(settings.ce_probability, settings.le_probability);
  if (!factors || settings.max_skew < Duration::zero()) {
    return fail(Error::kInvalidArgument);
  }
  return Adapter(std::move(settings), *factors, range);
}

std::optional<v1::PliRecord> Adapter::record(const Event& event, const UtcTime received) const {
  const Point& point = event.point;
  const double height = hae_known(point) ? point.hae_m : 0.0;
  const Result<frames::Geodetic> where =
      frames::Geodetic::make(Degrees(point.latitude_deg), Degrees(point.longitude_deg), Meters(height));
  if (!event.type.starts_with(kAtom) || !where) {
    return std::nullopt;
  }
  v1::PliRecord out;
  out.set_entity_id(event.uid);
  out.set_role(role(event.uid));
  out.set_source(v1::PLI_SOURCE_COT);
  set_time(out, event, received);
  out.mutable_position()->set_latitude_deg(where->latitude().value());
  out.mutable_position()->set_longitude_deg(where->longitude().value());
  out.mutable_position()->set_height_ellipsoid_m(where->height().value());
  if (event.track) {
    set_velocity(*out.mutable_velocity_enu_mps(), *where, *event.track, range_);
  }
  set_errors(out, point);
  out.set_fix_type(fix_type(event));
  return out;
}

void Adapter::set_time(v1::PliRecord& out, const Event& event, const UtcTime received) const {
  const bool trusted = event.time.has_value() && std::chrono::abs(*event.time - received) <= settings_.max_skew;
  out.set_valid_utc_ns(to_utc_ns(trusted ? *event.time : received));
  out.set_time_basis(trusted ? v1::PLI_TIME_BASIS_VEHICLE_GNSS : v1::PLI_TIME_BASIS_RECEIPT);
  out.set_received_utc_ns(to_utc_ns(received));
}

void Adapter::set_errors(v1::PliRecord& out, const Point& point) const {
  const std::optional<double> horizontal = sigma(point.ce_m, factors_.ce_factor);
  const std::optional<double> vertical = hae_known(point) ? sigma(point.le_m, factors_.le_factor) : std::nullopt;
  if (horizontal) {
    out.set_horizontal_sigma_m(*horizontal);
  }
  if (vertical) {
    out.set_vertical_sigma_m(*vertical);
  }
}

v1::EntityRole Adapter::role(const std::string& uid) const noexcept {
  const auto found =
      std::ranges::find_if(settings_.roles, [&uid](const RoleAssignment& assigned) { return assigned.uid == uid; });
  return found == settings_.roles.end() ? v1::ENTITY_ROLE_OTHER : found->role;
}

}  // namespace ics::cot
