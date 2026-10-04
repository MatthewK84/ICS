#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"

namespace ics::lattice {

// Lattice entity events (ICS-023): the JSON data of one server-sent event from
// Lattice's REST entity stream, POST /api/v1/entities/stream. Only the parts
// the adapter uses are kept:
//
//   eventType                          EVENT_TYPE_PREEXISTING, _CREATED, _UPDATE, ...
//   time                               when Lattice recorded the event (RFC 3339)
//   entity.entityId                    the entity's ID
//   entity.isLive                      false once the entity has expired
//   entity.location.position           latitudeDegrees, longitudeDegrees, altitudeHaeMeters
//   entity.location.velocityEnu        e, n, u in metres per second, at the entity
//   entity.locationUncertainty
//         .positionEnuCov              mxx, mxy, mxz, myy, myz, mzz in square metres
//   entity.provenance.sourceUpdateTime when the source last updated the entity
//
// Times are read from 1970 to 2200; any other time is left out.
//
// Lattice writes protobuf messages as JSON, which leaves out fields that hold
// zero, so a missing number inside a vector, matrix or position is 0. A
// position needs at least one of its coordinates, and altitudeHaeMeters may
// be missing, meaning unknown. A payload without an entity, such as a
// heartbeat, is not an entity event.

enum class EventType {
  kPreexisting,
  kCreated,
  kUpdate,
  kDeleted,
  // Any other event type, or none.
  kOther,
};

struct Position {
  double latitude_deg = 0.0;
  double longitude_deg = 0.0;
  // Height above the WGS84 ellipsoid: nothing when unknown.
  std::optional<double> hae_m;
};

// East, north and up.
struct Enu {
  double east = 0.0;
  double north = 0.0;
  double up = 0.0;
};

// The upper triangle of a position covariance in the entity's ENU frame
// (x east, y north, z up), in square metres.
struct Covariance {
  double xx = 0.0;
  double xy = 0.0;
  double xz = 0.0;
  double yy = 0.0;
  double yz = 0.0;
  double zz = 0.0;
};

struct Entity {
  std::string entity_id;
  // isLive: true unless the event says false.
  bool live = true;
  std::optional<Position> position;
  std::optional<Enu> velocity_enu_mps;
  std::optional<Covariance> position_covariance_m2;
  std::optional<UtcTime> source_update_time;
};

struct Event {
  EventType type = EventType::kOther;
  std::optional<UtcTime> time;
  Entity entity;
};

// The entity event in one event's data; nothing for a payload without an
// entity. Fails with Error::kMalformed when the data is not a JSON object, or
// its entity has no entityId.
[[nodiscard]] Result<std::optional<Event>> decode_event(std::string_view json);

}  // namespace ics::lattice
