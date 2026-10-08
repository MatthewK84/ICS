#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "ics/common/error.hpp"
#include "ics/flightlog/import.hpp"
#include "ics/retime/sorties.hpp"
#include "ics/timealign/align.hpp"
#include "ics/timealign/clock_fit.hpp"
#include "ics/v1/pli.pb.h"

namespace ics::retime {

// The model that times a sortie's records: its fit's, when its clock pairs
// lie on a straight line (timealign::straight). Otherwise the adapter's live
// times stand.
[[nodiscard]] std::optional<timealign::ClockModel> straight_model(const Result<timealign::ClockFit>& fit);

// A sortie's positions timed by its fit, with PLI_TIME_BASIS_VEHICLE_ALIGNED,
// when its clock pairs lie on a straight line; none otherwise.
[[nodiscard]] std::vector<v1::PliRecord> aligned_positions(const Sortie& sortie);

// How an onboard log's records are timed: by the fit of the sortie it
// overlaps, when that is straight, or otherwise by that sortie's clock pairs
// as the live adapter times its records. Never by the log's own GNSS times,
// which can sit tens of milliseconds from the pairs (ArduCopter's are 36 ms
// early, the lag from a fix to its logging). With no sortie, the log keeps
// the importer's own times.
struct LogTiming {
  std::optional<timealign::ClockModel> fit;
  std::optional<timealign::SteppedClock> pairs;
};

// A log matched to the captures.
struct TimedLog {
  // The sortie of the log's vehicle whose boot times overlap the log's most.
  std::optional<std::size_t> sortie;
  // That sortie's clock pairs; none without one.
  std::span<const timealign::ClockSample> pairs;
  Result<timealign::ClockFit> fit = fail(Error::kEmpty);
  LogTiming timing;
};

// Matches the log to the sortie its boot times overlap most, and times it.
// The pairs point into vehicles, which must outlive the result.
[[nodiscard]] TimedLog time_log(const flightlog::LogContents& contents, const Vehicles& vehicles);

// "fit", "pairs" or "log": what times the log's records.
[[nodiscard]] std::string_view timed_by(const LogTiming& timing) noexcept;

// The median of the log's own GNSS times less the times its timing gives the
// same boot times: how far the log's clock sits from the captures'. Nothing
// when no GNSS time has one.
[[nodiscard]] std::optional<std::int64_t> log_offset_ns(const flightlog::LogContents& contents,
                                                        const LogTiming& timing);

// A log's record or event, timed as timing says, or unchanged when it says
// nothing for its boot time.
[[nodiscard]] v1::PliRecord retimed(const v1::PliRecord& record, std::int64_t boot_us, const LogTiming& timing);
[[nodiscard]] v1::PliEvent retimed(const v1::PliEvent& event, std::int64_t boot_us, const LogTiming& timing);

}  // namespace ics::retime
