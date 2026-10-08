#include "ics/retime/retime.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "ics/common/check.hpp"
#include "ics/common/units.hpp"

namespace ics::retime {
namespace {

using timealign::ClockFit;
using timealign::ClockModel;
using timealign::ClockSample;

// The lowest and highest boot times of a sortie, or of a log.
struct Span {
  std::int64_t lowest = std::numeric_limits<std::int64_t>::max();
  std::int64_t highest = std::numeric_limits<std::int64_t>::min();

  void add(const std::int64_t boot_us) {
    lowest = std::min(lowest, boot_us);
    highest = std::max(highest, boot_us);
  }
  [[nodiscard]] std::int64_t overlap(const Span& other) const {
    return std::max<std::int64_t>(0, std::min(highest, other.highest) - std::max(lowest, other.lowest));
  }
};

[[nodiscard]] Span span_of(const Sortie& sortie) {
  Span out;
  for (const ClockSample& sample : sortie.samples) {
    out.add(sample.boot_us);
  }
  for (const Positioned& position : sortie.positions) {
    out.add(position.boot_us);
  }
  return out;
}

[[nodiscard]] Span span_of(const flightlog::LogContents& contents) {
  Span out;
  for (const flightlog::LogRecord& record : contents.states) {
    out.add(record.boot_us);
  }
  for (const flightlog::LogRecord& record : contents.gnss) {
    out.add(record.boot_us);
  }
  for (const flightlog::LogEvent& event : contents.events) {
    out.add(event.boot_us);
  }
  return out;
}

// The vehicle of the log's system, if the captures saw it.
[[nodiscard]] const Vehicle* vehicle_of(const flightlog::LogContents& contents, const Vehicles& vehicles) {
  const bool mavlink_id = contents.system_id <= std::numeric_limits<std::uint8_t>::max();
  const auto found = mavlink_id ? vehicles.find(static_cast<std::uint8_t>(contents.system_id)) : vehicles.end();
  return found == vehicles.end() ? nullptr : &found->second;
}

// The sortie of the vehicle whose boot times overlap the log's most.
[[nodiscard]] std::optional<std::size_t> matching_sortie(const flightlog::LogContents& contents,
                                                         const Vehicle& vehicle) {
  const Span log = span_of(contents);
  std::optional<std::size_t> best;
  std::int64_t best_overlap = 0;
  for (std::size_t index = 0; index < vehicle.sorties.size(); ++index) {
    const std::int64_t overlap = span_of(vehicle.sorties[index]).overlap(log);
    best = overlap > best_overlap ? std::optional(index) : best;
    best_overlap = std::max(best_overlap, overlap);
  }
  return best;
}

[[nodiscard]] std::optional<UtcTime> utc_at(const LogTiming& timing, const std::int64_t boot_us) {
  if (timing.fit) {
    return timing.fit->utc(boot_us);
  }
  return timing.pairs ? timing.pairs->utc(boot_us) : std::nullopt;
}

}  // namespace

std::optional<ClockModel> straight_model(const Result<ClockFit>& fit) {
  return fit && timealign::straight(*fit) ? std::optional(fit->model) : std::nullopt;
}

std::vector<v1::PliRecord> aligned_positions(const Sortie& sortie) {
  const std::optional<ClockModel> model = straight_model(timealign::fit_clock(sortie.samples));
  std::vector<v1::PliRecord> out;
  for (const Positioned& position : sortie.positions) {
    std::optional<v1::PliRecord> timed =
        model ? timealign::aligned(position.record, position.boot_us, *model) : std::nullopt;
    if (timed) {
      out.push_back(std::move(*timed));
    }
  }
  static_cast<void>(ics::check(out.size() <= sortie.positions.size()));
  return out;
}

TimedLog time_log(const flightlog::LogContents& contents, const Vehicles& vehicles) {
  const Vehicle* vehicle = vehicle_of(contents, vehicles);
  TimedLog out;
  out.sortie = vehicle != nullptr ? matching_sortie(contents, *vehicle) : std::nullopt;
  out.pairs = out.sortie ? std::span<const ClockSample>(vehicle->sorties[*out.sortie].samples)
                         : std::span<const ClockSample>();
  out.fit = timealign::fit_clock(out.pairs);
  Result<timealign::SteppedClock> stepped = timealign::SteppedClock::make(out.pairs);
  out.timing = LogTiming{.fit = straight_model(out.fit),
                         .pairs = stepped ? std::optional(std::move(*stepped)) : std::nullopt};
  return out;
}

std::string_view timed_by(const LogTiming& timing) noexcept {
  if (timing.fit) {
    return "fit";
  }
  return timing.pairs ? "pairs" : "log";
}

std::optional<std::int64_t> log_offset_ns(const flightlog::LogContents& contents, const LogTiming& timing) {
  std::vector<std::int64_t> offsets;
  for (const flightlog::GnssTime& time : contents.gnss_times) {
    const std::optional<UtcTime> capture = utc_at(timing, time.boot_us);
    if (capture) {
      offsets.push_back(to_utc_ns(time.utc) - to_utc_ns(*capture));
    }
  }
  if (offsets.empty()) {
    return std::nullopt;
  }
  const auto middle = offsets.begin() + static_cast<std::ptrdiff_t>(offsets.size() / 2);
  std::ranges::nth_element(offsets, middle);
  return *middle;
}

v1::PliRecord retimed(const v1::PliRecord& record, const std::int64_t boot_us, const LogTiming& timing) {
  const std::optional<v1::PliRecord> out = timing.fit     ? timealign::aligned(record, boot_us, *timing.fit)
                                           : timing.pairs ? timealign::stepped(record, boot_us, *timing.pairs)
                                                          : std::nullopt;
  return out ? *out : record;
}

v1::PliEvent retimed(const v1::PliEvent& event, const std::int64_t boot_us, const LogTiming& timing) {
  const std::optional<v1::PliEvent> out = timing.fit     ? timealign::aligned(event, boot_us, *timing.fit)
                                          : timing.pairs ? timealign::stepped(event, boot_us, *timing.pairs)
                                                         : std::nullopt;
  return out ? *out : event;
}

}  // namespace ics::retime
