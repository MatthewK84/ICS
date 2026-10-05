#pragma once

#include <cstddef>
#include <string>

#include "ics/frames/egm96.hpp"
#include "ics/frames/enu.hpp"
#include "ics/timealign/check.hpp"
#include "sorties.hpp"

namespace ics::time_align {

// Writes each sortie's fit, each vehicle's latency, and with records, every
// record. A sortie whose clock pairs lie on a straight line has its records
// timed by its fit, and only they count towards the latency; any other keeps
// the adapter's live times.
void report(const Vehicles& vehicles, bool records);

// Imports an onboard log and writes how it is timed: from the clock pairs of
// the sortie its boot times overlap, by their fit when straight and otherwise
// as the live adapter times records, or by its own GNSS times when no sortie
// matches. With records, writes its records and events so timed. False when
// it cannot be read.
[[nodiscard]] bool report_log(std::size_t index, const std::string& path, const Vehicles& vehicles,
                              const frames::Egm96& geoid, const frames::EnuFrame& range, bool records);

// Writes the drift check of each sortie and a verdict. The check applies only
// to a sortie whose clock has no drift of its own (its offsets within 0.5 ms
// of their mean); the verdict is true when it applied to at least one and each
// one it applied to is within 1 ms.
[[nodiscard]] bool check(const Vehicles& vehicles, const timealign::Injection& injection,
                         const timealign::Withholding& withholding);

}  // namespace ics::time_align
