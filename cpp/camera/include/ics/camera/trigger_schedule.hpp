#pragma once

#include "ics/common/units.hpp"

namespace ics::camera {

// When an emulated camera's triggers come, by its IRIG clock: the first at
// first, and each later one interval after the one before.
struct TriggerSchedule {
  UtcTime first{};
  Duration interval{};
};

}  // namespace ics::camera
