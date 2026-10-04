#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/frames/enu.hpp"
#include "ics/lattice/event.hpp"
#include "ics/v1/common.pb.h"
#include "ics/v1/pli.pb.h"

namespace ics::lattice {

// The Lattice adapter (ICS-023): turns a Lattice entity event into an
// ics.v1.PliRecord.
//
// - Only PREEXISTING, CREATED and UPDATE events of a live entity with a
//   position give a record; a deleted or expired entity gives nothing, and
//   staleness is left to ics-plid (ICS-030).
// - entity_id is the entity's ID. The role comes from the settings, and is
//   ENTITY_ROLE_OTHER for an entity not listed.
// - The position is the entity's latitude, longitude and altitudeHaeMeters,
//   already above the WGS84 ellipsoid. Without altitudeHaeMeters the height
//   is 0 and not valid, and the vertical sigma is left unset.
// - velocityEnu, in the entity's own ENU frame, is rotated into the range ENU
//   frame; without it the velocity is left unset.
// - The horizontal sigma is the square root of the larger eigenvalue of the
//   east-north block of positionEnuCov, the one-sigma semi-major axis; the
//   vertical sigma is the square root of its up variance. Without a valid
//   covariance both are left unset.
// - Lattice reports no GNSS fix, so the fix type is FIX_TYPE_OTHER.
// - Time: the source's sourceUpdateTime, with PLI_TIME_BASIS_VEHICLE_GNSS,
//   when it is within max_skew of the time ICS received the event, as for
//   CoT (ICS-022). Otherwise the receipt time, with PLI_TIME_BASIS_RECEIPT.

struct RoleAssignment {
  std::string entity_id;
  v1::EntityRole role = v1::ENTITY_ROLE_UNSPECIFIED;
};

struct AdapterSettings {
  std::vector<RoleAssignment> roles{};
  Duration max_skew = std::chrono::seconds(30);
};

// Metres per second, for velocities.
struct MeterPerSecond;
using MetersPerSecond = Quantity<MeterPerSecond>;

class Adapter {
 public:
  // Fails with Error::kInvalidArgument for a negative max_skew.
  [[nodiscard]] static Result<Adapter> make(AdapterSettings settings, const frames::EnuFrame& range);

  // The record for an event received at the time given, or nothing.
  [[nodiscard]] std::optional<v1::PliRecord> record(const Event& event, UtcTime received) const;

 private:
  Adapter(AdapterSettings settings, const frames::EnuFrame& range);

  void set_time(v1::PliRecord& out, const Entity& entity, UtcTime received) const;
  [[nodiscard]] v1::EntityRole role(const std::string& entity_id) const noexcept;

  AdapterSettings settings_;
  frames::EnuFrame range_;
};

}  // namespace ics::lattice
