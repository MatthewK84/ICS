#pragma once

#include <cstddef>
#include <string>

#include "ics/frames/egm96.hpp"
#include "ics/frames/enu.hpp"
#include "ics/timealign/check.hpp"
#include "sorties.hpp"

namespace ics::time_align {

// Writes each sortie's fit, each vehicle's latency, and with records, every
// record timed by its sortie's fit.
void report(const Vehicles& vehicles, bool records);

// Imports an onboard log and writes its fit, with the sortie its boot times
// overlap; false when it cannot be read.
[[nodiscard]] bool report_log(std::size_t index, const std::string& path, const Vehicles& vehicles,
                              const frames::Egm96& geoid, const frames::EnuFrame& range, bool records);

// Writes the drift check of each sortie and a verdict: true when at least one
// sortie was checked and every one checked is within 1 ms.
[[nodiscard]] bool check(const Vehicles& vehicles, const timealign::Injection& injection,
                         const timealign::Withholding& withholding);

}  // namespace ics::time_align
