#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "ics/mavlink/messages.hpp"

namespace ics::mavlink {

// The flight mode a HEARTBEAT reports, by name (ICS-021). custom_mode means
// something different to each autopilot; the names are those of the two in
// the SITL rig (ICS-018):
// - PX4 (MAV_AUTOPILOT_PX4): the main mode, and the sub-mode where there is
//   one, as PX4 names them: "POSCTL", "AUTO.MISSION".
// - ArduCopter (MAV_AUTOPILOT_ARDUPILOTMEGA on a multirotor or helicopter):
//   "LOITER", "GUIDED".
// Any other autopilot or vehicle, or a mode number neither knows, gives the
// number: "custom mode 7".
[[nodiscard]] std::string mode_name(const Heartbeat& heartbeat);

// An ArduCopter flight mode's name, by its number, as in HEARTBEAT's
// custom_mode and the DataFlash MODE message (ICS-025).
[[nodiscard]] std::optional<std::string_view> copter_mode_name(std::uint32_t mode);

}  // namespace ics::mavlink
