#pragma once

#include <chrono>
#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/frames/egm96.hpp"
#include "ics/frames/enu.hpp"
#include "ics/sapient/registration.hpp"
#include "ics/sapient/stream.hpp"
#include "ics/v1/common.pb.h"
#include "ics/v1/pli.pb.h"

namespace ics::sapient {

// The SAPIENT adapter (ICS-024): turns the detection reports of SAPIENT
// nodes (BSI Flex 335 v2.0) into ics.v1.PliRecord.
//
// - A Registration sets its node's units (registration.hpp): the default UTM
//   zone, and the units of its velocities. A node that registered before the
//   adapter started has neither until it registers again.
// - Each DetectionReport with a location gives a record; location.hpp says
//   how it is converted. A report with a range and bearing instead, or no
//   location, or one that cannot be converted, gives nothing and is counted.
// - entity_id is the report's object_id. The role comes from the settings,
//   matched against the object_id or the report's id (such as a tail
//   number), and is ENTITY_ROLE_OTHER for an object not listed.
// - enu_velocity is in the object's own ENU frame, in its node's registered
//   units; it is rotated into the range ENU frame. A missing up rate is
//   taken as 0. Without registered units the velocity is left unset.
// - SAPIENT reports no GNSS fix, so the fix type is FIX_TYPE_OTHER.
// - Time: the message's timestamp, with PLI_TIME_BASIS_VEHICLE_GNSS, when it
//   is within max_skew of the time ICS received the message, as for CoT
//   (ICS-022). Otherwise the receipt time, with PLI_TIME_BASIS_RECEIPT.
// Other messages, such as status reports and tasks, give nothing.

struct RoleAssignment {
  // A detection's object_id, or its id.
  std::string id;
  v1::EntityRole role = v1::ENTITY_ROLE_UNSPECIFIED;
};

struct AdapterSettings {
  std::vector<RoleAssignment> roles{};
  Duration max_skew = std::chrono::seconds(30);
  // The most nodes whose units are kept.
  std::size_t max_nodes = 1024;
};

struct AdapterCounts {
  // Detection reports with a range and bearing, which ICS does not convert.
  std::size_t range_bearing = 0;
  // Detection reports with no location, or one that could not be converted.
  std::size_t unlocated = 0;
  // Detection reports with no object_id.
  std::size_t unidentified = 0;
  // Registrations not kept: past max_nodes, or with a node_id that is empty
  // or longer than kMaxNodeIdBytes.
  std::size_t registrations_ignored = 0;
};

// Metres per second, for velocities.
struct MeterPerSecond;
using MetersPerSecond = Quantity<MeterPerSecond>;

class Adapter {
 public:
  // A node_id is a UUID, 36 characters.
  static constexpr std::size_t kMaxNodeIdBytes = 64;

  // The geoid is borrowed and must outlive the adapter. Fails with
  // Error::kInvalidArgument for a negative max_skew.
  [[nodiscard]] static Result<Adapter> make(AdapterSettings settings, const frames::Egm96& geoid,
                                            const frames::EnuFrame& range);

  // Takes a message received at the time given, and gives the record for a
  // detection report, or nothing.
  [[nodiscard]] std::optional<v1::PliRecord> receive(const Message& message, UtcTime received);

  [[nodiscard]] const AdapterCounts& counts() const noexcept { return counts_; }

 private:
  Adapter(AdapterSettings settings, const frames::Egm96& geoid, const frames::EnuFrame& range);

  void registration(const Message& message);
  [[nodiscard]] std::optional<v1::PliRecord> detection(const Message& message, UtcTime received);
  void set_time(v1::PliRecord& out, const Message& message, UtcTime received) const;
  void set_velocity(v1::PliRecord& out, const frames::Geodetic& where, const Message& message) const;
  // A node's units: none for a node that has not registered.
  [[nodiscard]] NodeUnits units(const std::string& node_id) const;
  [[nodiscard]] v1::EntityRole role(const std::string& object_id, const std::string& id) const noexcept;

  AdapterSettings settings_;
  std::reference_wrapper<const frames::Egm96> geoid_;
  frames::EnuFrame range_;
  std::map<std::string, NodeUnits, std::less<>> nodes_;
  AdapterCounts counts_;
};

}  // namespace ics::sapient
