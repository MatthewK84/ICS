#include "ics/lattice/event.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <google/protobuf/struct.pb.h>
#include <google/protobuf/timestamp.pb.h>
#include <google/protobuf/util/json_util.h>
#include <google/protobuf/util/time_util.h>

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"

namespace ics::lattice {
namespace {

using google::protobuf::Struct;
using google::protobuf::Value;

struct NamedType {
  std::string_view name;
  EventType type = EventType::kOther;
};

constexpr std::array<NamedType, 4> kEventTypes{{
    {"EVENT_TYPE_PREEXISTING", EventType::kPreexisting},
    {"EVENT_TYPE_CREATED", EventType::kCreated},
    {"EVENT_TYPE_UPDATE", EventType::kUpdate},
    {"EVENT_TYPE_DELETED", EventType::kDeleted},
}};

// The last second of 2200. Times from 1970 to 2200 are read, as for CoT
// (ICS-022), so the difference of any two fits a Duration.
constexpr std::int64_t kLastSecond = 7'289'654'399;

// The member named key, or nothing.
[[nodiscard]] const Value* member(const Struct& object, const std::string_view key) {
  const auto found = object.fields().find(std::string(key));
  return found == object.fields().end() ? nullptr : &found->second;
}

// The object member named key, or nothing when it is missing or not an object.
[[nodiscard]] const Struct* object(const Struct& parent, const std::string_view key) {
  const Value* value = member(parent, key);
  return value != nullptr && value->kind_case() == Value::kStructValue ? &value->struct_value() : nullptr;
}

[[nodiscard]] bool is_finite_number(const Value& value) {
  return value.kind_case() == Value::kNumberValue && std::isfinite(value.number_value());
}

// A finite number, or nothing when it is missing or anything else.
[[nodiscard]] std::optional<double> number(const Struct& parent, const std::string_view key) {
  const Value* value = member(parent, key);
  if (value == nullptr || !is_finite_number(*value)) {
    return std::nullopt;
  }
  return value->number_value();
}

// A number protobuf leaves out when it is zero: 0 when missing, nothing when
// it is anything but a finite number.
[[nodiscard]] std::optional<double> zero_or_number(const Struct& parent, const std::string_view key) {
  return member(parent, key) == nullptr ? std::optional<double>(0.0) : number(parent, key);
}

[[nodiscard]] std::optional<UtcTime> time(const Struct& parent, const std::string_view key) {
  const Value* value = member(parent, key);
  google::protobuf::Timestamp stamp;
  if (value == nullptr || value->kind_case() != Value::kStringValue ||
      !google::protobuf::util::TimeUtil::FromString(value->string_value(), &stamp) || stamp.seconds() < 0 ||
      stamp.seconds() > kLastSecond) {
    return std::nullopt;
  }
  return UtcTime(std::chrono::seconds(stamp.seconds()) + std::chrono::nanoseconds(stamp.nanos()));
}

[[nodiscard]] EventType event_type(const Struct& root) {
  const Value* value = member(root, "eventType");
  const std::string_view name = value != nullptr && value->kind_case() == Value::kStringValue
                                    ? std::string_view(value->string_value())
                                    : std::string_view{};
  const auto found = std::ranges::find(kEventTypes, name, &NamedType::name);
  return found == kEventTypes.end() ? EventType::kOther : found->type;
}

[[nodiscard]] std::optional<Position> position(const Struct& location) {
  const Struct* fields = object(location, "position");
  if (fields == nullptr) {
    return std::nullopt;
  }
  const bool placed = member(*fields, "latitudeDegrees") != nullptr || member(*fields, "longitudeDegrees") != nullptr;
  const std::optional<double> latitude = zero_or_number(*fields, "latitudeDegrees");
  const std::optional<double> longitude = zero_or_number(*fields, "longitudeDegrees");
  if (!placed || !latitude || !longitude) {
    return std::nullopt;
  }
  return Position{.latitude_deg = *latitude, .longitude_deg = *longitude, .hae_m = number(*fields, "altitudeHaeMeters")};
}

[[nodiscard]] std::optional<Enu> enu(const Struct* fields) {
  if (fields == nullptr) {
    return std::nullopt;
  }
  const std::optional<double> east = zero_or_number(*fields, "e");
  const std::optional<double> north = zero_or_number(*fields, "n");
  const std::optional<double> up = zero_or_number(*fields, "u");
  if (!east || !north || !up) {
    return std::nullopt;
  }
  return Enu{.east = *east, .north = *north, .up = *up};
}

[[nodiscard]] std::optional<Covariance> covariance(const Struct* fields) {
  if (fields == nullptr) {
    return std::nullopt;
  }
  constexpr std::array<std::string_view, 6> kKeys{"mxx", "mxy", "mxz", "myy", "myz", "mzz"};
  std::array<double, kKeys.size()> values{};
  for (std::size_t i = 0; i < kKeys.size(); ++i) {
    const std::optional<double> value = zero_or_number(*fields, kKeys.at(i));
    if (!value) {
      return std::nullopt;
    }
    values.at(i) = *value;
  }
  return Covariance{.xx = values[0], .xy = values[1], .xz = values[2], .yy = values[3], .yz = values[4], .zz = values[5]};
}

[[nodiscard]] Result<Entity> entity(const Struct& fields) {
  const Value* id = member(fields, "entityId");
  if (id == nullptr || id->kind_case() != Value::kStringValue || id->string_value().empty()) {
    return fail(Error::kMalformed);
  }
  const Value* live = member(fields, "isLive");
  Entity out{.entity_id = id->string_value(),
             .live = live == nullptr || live->kind_case() != Value::kBoolValue || live->bool_value(),
             .position = std::nullopt,
             .velocity_enu_mps = std::nullopt,
             .position_covariance_m2 = std::nullopt,
             .source_update_time = std::nullopt};
  if (const Struct* location = object(fields, "location"); location != nullptr) {
    out.position = position(*location);
    out.velocity_enu_mps = enu(object(*location, "velocityEnu"));
  }
  if (const Struct* uncertainty = object(fields, "locationUncertainty"); uncertainty != nullptr) {
    out.position_covariance_m2 = covariance(object(*uncertainty, "positionEnuCov"));
  }
  if (const Struct* provenance = object(fields, "provenance"); provenance != nullptr) {
    out.source_update_time = time(*provenance, "sourceUpdateTime");
  }
  return out;
}

}  // namespace

Result<std::optional<Event>> decode_event(const std::string_view json) {
  Struct root;
  if (!google::protobuf::util::JsonStringToMessage(json, &root).ok()) {
    return fail(Error::kMalformed);
  }
  const Struct* fields = object(root, "entity");
  if (fields == nullptr) {
    return std::optional<Event>{};
  }
  Result<Entity> decoded = entity(*fields);
  if (!decoded) {
    return fail(decoded.error());
  }
  return std::optional<Event>(Event{.type = event_type(root), .time = time(root, "time"), .entity = std::move(*decoded)});
}

}  // namespace ics::lattice
