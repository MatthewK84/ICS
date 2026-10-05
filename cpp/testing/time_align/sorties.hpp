#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "ics/common/error.hpp"
#include "ics/frames/egm96.hpp"
#include "ics/frames/enu.hpp"
#include "ics/timealign/clock_fit.hpp"
#include "ics/timealign/sortie.hpp"
#include "ics/v1/pli.pb.h"

namespace ics::time_align {

// A position record from the capture, with the boot time MAVLink gave it.
struct Positioned {
  v1::PliRecord record;
  std::int64_t boot_us = 0;
};

// One boot of one vehicle, as the capture saw it.
struct Sortie {
  std::vector<timealign::ClockSample> samples;
  std::vector<Positioned> positions;
};

struct Vehicle {
  timealign::SortieCounter counter;
  std::vector<Sortie> sorties;
};

// Each MAVLink system's sorties, by system ID.
using Vehicles = std::map<std::uint8_t, Vehicle>;

// Replays a TAP capture of Ethernet frames through the MAVLink adapter and
// sorts what it gives by vehicle and sortie. On failure, reason says why.
[[nodiscard]] Result<Vehicles> collect(const std::filesystem::path& capture, const frames::Egm96& geoid,
                                       const frames::EnuFrame& range, std::string& reason);

}  // namespace ics::time_align
