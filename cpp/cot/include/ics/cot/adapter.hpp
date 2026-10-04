#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/cot/event.hpp"
#include "ics/cot/uncertainty.hpp"
#include "ics/frames/enu.hpp"
#include "ics/v1/common.pb.h"
#include "ics/v1/pli.pb.h"

namespace ics::cot {

// The CoT adapter (ICS-022): turns a CoT event into an ics.v1.PliRecord.
//
// - Only atoms (a type starting "a-") are positions; every other event, such
//   as chat (b-t-f), a marker (b-m-p) or a delete (t-x-d-d), gives nothing.
// - entity_id is the uid. The role comes from the settings, and is
//   ENTITY_ROLE_OTHER for a uid not listed: affiliation is not role.
// - The position is the point's latitude, longitude and hae, which CoT
//   already gives above the WGS84 ellipsoid. A track's course and speed
//   become a horizontal velocity in the range ENU frame; without a track the
//   velocity is left unset.
// - ce and le become one-sigma errors at the settings' probabilities
//   (uncertainty.hpp); an unknown value leaves its sigma unset, as an unknown
//   hae does the vertical one.
// - The fix: a machine GPS position (how "m-g...") is three-dimensional, or
//   two-dimensional when hae is unknown, in which case the height is 0 and not
//   valid. Any other how, such as human-entered or fused, is FIX_TYPE_OTHER.
// - Time: the event's time attribute, with PLI_TIME_BASIS_VEHICLE_GNSS, when
//   it is within max_skew of the time ICS received it. Otherwise the receipt
//   time, with PLI_TIME_BASIS_RECEIPT, flagging a sender whose time is
//   missing, malformed or wrong.

struct RoleAssignment {
  std::string uid;
  v1::EntityRole role = v1::ENTITY_ROLE_UNSPECIFIED;
};

struct AdapterSettings {
  std::vector<RoleAssignment> roles{};
  // The probability ce and le are given at: 0.90 is CE90 and LE90.
  double ce_probability = 0.90;
  double le_probability = 0.90;
  Duration max_skew = std::chrono::seconds(30);
};

// Metres per second, for velocities.
struct MeterPerSecond;
using MetersPerSecond = Quantity<MeterPerSecond>;

class Adapter {
 public:
  // Fails with Error::kInvalidArgument for a probability not strictly between
  // 0 and 1, or a negative max_skew.
  [[nodiscard]] static Result<Adapter> make(AdapterSettings settings, const frames::EnuFrame& range);

  // The record for an event received at the time given, or nothing when the
  // event is not an atom.
  [[nodiscard]] std::optional<v1::PliRecord> record(const Event& event, UtcTime received) const;

 private:
  Adapter(AdapterSettings settings, SigmaFactors factors, const frames::EnuFrame& range);

  void set_time(v1::PliRecord& out, const Event& event, UtcTime received) const;
  void set_errors(v1::PliRecord& out, const Point& point) const;
  [[nodiscard]] v1::EntityRole role(const std::string& uid) const noexcept;

  AdapterSettings settings_;
  SigmaFactors factors_;
  frames::EnuFrame range_;
};

}  // namespace ics::cot
