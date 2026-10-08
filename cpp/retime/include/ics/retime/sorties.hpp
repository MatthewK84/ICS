#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <vector>

#include "ics/common/error.hpp"
#include "ics/frames/egm96.hpp"
#include "ics/frames/enu.hpp"
#include "ics/mavlink/adapter.hpp"
#include "ics/timealign/clock_fit.hpp"
#include "ics/timealign/sortie.hpp"
#include "ics/v1/pli.pb.h"

namespace ics::retime {

// A position record from a capture, with the boot time MAVLink gave it.
struct Positioned {
  v1::PliRecord record;
  std::int64_t boot_us = 0;
};

// One boot of one vehicle, as the captures saw it: its clock pairs
// (SYSTEM_TIME) and its positions.
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

// Replays TAP captures of Ethernet frames, in the order given, through one
// MAVLink adapter, and sorts what it gives by vehicle and sortie (ICS-026,
// ICS-030). Fails, saying why in reason: as Capture::open_file does;
// kMalformed for a capture that does not hold Ethernet frames; kUnreadable
// for one that cannot be read to its end.
[[nodiscard]] Result<Vehicles> collect(std::span<const std::filesystem::path> captures,
                                       const mavlink::AdapterSettings& settings, const frames::Egm96& geoid,
                                       const frames::EnuFrame& range, std::string& reason);

}  // namespace ics::retime
