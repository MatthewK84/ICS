#include "output.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <google/protobuf/util/json_util.h>

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/flightlog/import.hpp"
#include "ics/frames/egm96.hpp"
#include "ics/frames/enu.hpp"
#include "ics/timealign/align.hpp"
#include "ics/timealign/check.hpp"
#include "ics/timealign/clock_fit.hpp"
#include "ics/timealign/latency.hpp"
#include "sorties.hpp"

namespace ics::time_align {
namespace {

using timealign::ClockFit;
using timealign::ClockModel;
using timealign::ClockSample;
using timealign::Outcome;

// The JSON of a message; "null" if protobuf cannot write it.
[[nodiscard]] std::string json(const google::protobuf::Message& message) {
  google::protobuf::util::JsonPrintOptions options;
  options.preserve_proto_field_names = true;
  options.always_print_fields_with_no_presence = true;
  std::string out;
  return google::protobuf::util::MessageToJsonString(message, &out, options).ok() ? out : std::string("null");
}

// A fit's JSON fields, or why there is none.
[[nodiscard]] std::string fit_fields(const Result<ClockFit>& fit) {
  if (!fit) {
    return R"("fitted":false,"error":")" + std::string(to_string(fit.error())) + "\"";
  }
  std::array<char, 512> text{};
  const int written = std::snprintf(
      text.data(), text.size(),
      R"("fitted":true,"straight":%s,"used":%zu,"rejected":%zu,"origin_boot_us":%lld,"origin_utc_ns":%lld,)"
      R"("drift_ppm":%.6f,"residual_rms_ns":%.0f,"residual_max_ns":%.0f,"first_boot_us":%lld,"last_boot_us":%lld)",
      timealign::straight(*fit) ? "true" : "false", fit->used, fit->rejected,
      static_cast<long long>(fit->model.origin_boot_us()), static_cast<long long>(to_utc_ns(fit->model.origin_utc())),
      fit->model.drift_ppm(), fit->residual_rms.count(), fit->residual_max.count(),
      static_cast<long long>(fit->first_boot_us), static_cast<long long>(fit->last_boot_us));
  return std::string(text.data(), static_cast<std::size_t>(std::max(written, 0)));
}

// The model that times a sortie's records: its fit's, when its clock pairs lie
// on a straight line. Otherwise the adapter's live times stand.
[[nodiscard]] std::optional<ClockModel> straight_model(const Result<ClockFit>& fit) {
  return fit && timealign::straight(*fit) ? std::optional(fit->model) : std::nullopt;
}

void add_latencies(const Sortie& sortie, const ClockModel& model, std::vector<Duration>& out) {
  for (const Positioned& position : sortie.positions) {
    const std::optional<UtcTime> valid = model.utc(position.boot_us);
    if (valid) {
      out.push_back(utc_from_ns(position.record.received_utc_ns()) - *valid);
    }
  }
}

void print_latency(const unsigned system, std::vector<Duration> latencies) {
  const std::optional<timealign::LatencyStats> stats = timealign::summarize_latency(std::move(latencies));
  if (!stats) {
    std::printf(R"({"kind":"latency","system":%u,"count":0})" "\n", system);
    return;
  }
  std::printf(R"({"kind":"latency","system":%u,"count":%zu,"min_ns":%lld,"median_ns":%lld,"p95_ns":%lld,"max_ns":%lld})"
              "\n",
              system, stats->count, static_cast<long long>(stats->min.count()),
              static_cast<long long>(stats->median.count()), static_cast<long long>(stats->p95.count()),
              static_cast<long long>(stats->max.count()));
}

void print_records(const unsigned system, const std::size_t index, const Sortie& sortie,
                   const std::optional<ClockModel>& model) {
  for (const Positioned& position : sortie.positions) {
    const std::optional<v1::PliRecord> timed =
        model ? timealign::aligned(position.record, position.boot_us, *model) : std::nullopt;
    std::printf(R"({"kind":"record","system":%u,"sortie":%zu,"boot_us":%lld,"record":%s})" "\n", system, index,
                static_cast<long long>(position.boot_us), json(timed ? *timed : position.record).c_str());
  }
}

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

// The sortie of the log's vehicle whose boot times overlap the log's most.
[[nodiscard]] std::optional<std::size_t> matching_sortie(const flightlog::LogContents& contents,
                                                         const Vehicles& vehicles) {
  const bool mavlink_id = contents.system_id <= std::numeric_limits<std::uint8_t>::max();
  const auto vehicle = mavlink_id ? vehicles.find(static_cast<std::uint8_t>(contents.system_id)) : vehicles.end();
  if (vehicle == vehicles.end()) {
    return std::nullopt;
  }
  const Span log = span_of(contents);
  std::optional<std::size_t> best;
  std::int64_t best_overlap = 0;
  for (std::size_t index = 0; index < vehicle->second.sorties.size(); ++index) {
    const std::int64_t overlap = span_of(vehicle->second.sorties[index]).overlap(log);
    best = overlap > best_overlap ? std::optional(index) : best;
    best_overlap = std::max(best_overlap, overlap);
  }
  return best;
}

[[nodiscard]] std::optional<std::string> read_file(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) {
    return std::nullopt;
  }
  return std::string{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

// How a log's records are timed: by the fit of the sortie it overlaps, when
// that is straight, or otherwise by that sortie's clock pairs as the live
// adapter times its records. Never by the log's own GNSS times, which can sit
// tens of milliseconds from the pairs (ArduCopter's are 36 ms early, the lag
// from a fix to its logging). With no sortie, the log keeps the importer's
// own times.
struct LogTiming {
  std::optional<ClockModel> fit;
  std::optional<timealign::SteppedClock> pairs;
};

[[nodiscard]] LogTiming log_timing(const Result<ClockFit>& fit, const std::span<const ClockSample> pairs) {
  Result<timealign::SteppedClock> stepped = timealign::SteppedClock::make(pairs);
  return LogTiming{.fit = straight_model(fit), .pairs = stepped ? std::optional(std::move(*stepped)) : std::nullopt};
}

[[nodiscard]] const char* timed_by(const LogTiming& timing) {
  if (timing.fit) {
    return "fit";
  }
  return timing.pairs ? "pairs" : "log";
}

[[nodiscard]] std::optional<UtcTime> utc_at(const LogTiming& timing, const std::int64_t boot_us) {
  if (timing.fit) {
    return timing.fit->utc(boot_us);
  }
  return timing.pairs ? timing.pairs->utc(boot_us) : std::nullopt;
}

// The median of the log's own GNSS times less the times its timing gives the
// same boot times: how far the log's clock sits from the capture's.
[[nodiscard]] std::optional<std::int64_t> log_offset_ns(const flightlog::LogContents& contents,
                                                        const LogTiming& timing) {
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

[[nodiscard]] v1::PliRecord retimed(const v1::PliRecord& record, const std::int64_t boot_us,
                                    const LogTiming& timing) {
  const std::optional<v1::PliRecord> out = timing.fit     ? timealign::aligned(record, boot_us, *timing.fit)
                                           : timing.pairs ? timealign::stepped(record, boot_us, *timing.pairs)
                                                          : std::nullopt;
  return out ? *out : record;
}

[[nodiscard]] v1::PliEvent retimed(const v1::PliEvent& event, const std::int64_t boot_us, const LogTiming& timing) {
  const std::optional<v1::PliEvent> out = timing.fit     ? timealign::aligned(event, boot_us, *timing.fit)
                                          : timing.pairs ? timealign::stepped(event, boot_us, *timing.pairs)
                                                         : std::nullopt;
  return out ? *out : event;
}

void print_log_records(const std::size_t index, const flightlog::LogContents& contents, const LogTiming& timing) {
  const auto print = [index, &timing](const char* kind, const flightlog::LogRecord& logged) {
    std::printf(R"({"kind":"%s","log":%zu,"boot_us":%lld,"record":%s})" "\n", kind, index,
                static_cast<long long>(logged.boot_us), json(retimed(logged.record, logged.boot_us, timing)).c_str());
  };
  std::ranges::for_each(contents.states, [&print](const flightlog::LogRecord& r) { print("log_record", r); });
  std::ranges::for_each(contents.gnss, [&print](const flightlog::LogRecord& r) { print("log_record", r); });
  for (const flightlog::LogEvent& logged : contents.events) {
    std::printf(R"({"kind":"log_event","log":%zu,"boot_us":%lld,"event":%s})" "\n", index,
                static_cast<long long>(logged.boot_us), json(retimed(logged.event, logged.boot_us, timing)).c_str());
  }
}

// The sorties a check applied to, those whose clocks drift of their own
// accord, and whether every one it applied to was within the limit.
struct Tally {
  std::size_t checked = 0;
  std::size_t drifting = 0;
  bool passed = true;
};

// One sortie's check line. A check that could not run (too few samples, say)
// is "unchecked".
void print_check(const unsigned system, const std::size_t index, const std::size_t positions,
                 const Result<timealign::DriftCheck>& result, const std::optional<Outcome> outcome) {
  constexpr std::array<const char*, 3> kOutcomes{"within", "beyond", "drifting"};
  std::printf(R"({"kind":"check","system":%u,"sortie":%zu,"positions":%zu,"outcome":"%s","max_error_ns":%lld,)"
              R"("reference_spread_ns":%.0f,"sent_residual_rms_ns":%.0f,%s})" "\n",
              system, index, positions, outcome ? kOutcomes.at(static_cast<std::size_t>(*outcome)) : "unchecked",
              result ? static_cast<long long>(result->max_error.count()) : -1LL,
              result ? result->reference_spread.count() : -1.0, result ? result->sent.residual_rms.count() : -1.0,
              fit_fields(result ? Result<ClockFit>(result->fit) : fail(result.error())).c_str());
}

}  // namespace

void report(const Vehicles& vehicles, const bool records) {
  // Not a structured binding: CodeQL drops the body of a range-for that
  // declares one over this map, and with it every call the body makes.
  for (const auto& entry : vehicles) {
    const unsigned system = entry.first;
    const Vehicle& vehicle = entry.second;
    std::vector<Duration> latencies;
    for (std::size_t index = 0; index < vehicle.sorties.size(); ++index) {
      const Sortie& sortie = vehicle.sorties[index];
      const Result<ClockFit> fit = timealign::fit_clock(sortie.samples);
      const std::optional<ClockModel> model = straight_model(fit);
      std::printf(R"({"kind":"sortie","system":%u,"sortie":%zu,"samples":%zu,"positions":%zu,%s})" "\n",
                  system, index, sortie.samples.size(), sortie.positions.size(), fit_fields(fit).c_str());
      if (model) {
        add_latencies(sortie, *model, latencies);
      }
      if (records) {
        print_records(system, index, sortie, model);
      }
    }
    print_latency(system, std::move(latencies));
  }
}

bool report_log(const std::size_t index, const std::string& path, const Vehicles& vehicles,
                const frames::Egm96& geoid, const frames::EnuFrame& range, const bool records) {
  const std::optional<std::string> bytes = read_file(path);
  const Result<flightlog::LogContents> contents =
      bytes ? flightlog::import_log(std::as_bytes(std::span<const char>(*bytes)), {}, geoid, range)
            : fail(Error::kUnreadable);
  if (!contents) {
    return false;
  }
  const std::optional<std::size_t> sortie = matching_sortie(*contents, vehicles);
  const std::span<const ClockSample> pairs =
      sortie ? std::span<const ClockSample>(
                   vehicles.at(static_cast<std::uint8_t>(contents->system_id)).sorties[*sortie].samples)
             : std::span<const ClockSample>();
  const Result<ClockFit> fit = timealign::fit_clock(pairs);
  const LogTiming timing = log_timing(fit, pairs);
  const std::optional<std::int64_t> offset = log_offset_ns(*contents, timing);
  const std::string offset_text = offset ? std::to_string(*offset) : std::string("null");
  std::printf(R"({"kind":"log","log":%zu,"system":%u,"sortie":%lld,"samples":%zu,"log_gnss_times":%zu,)"
              R"("timed_by":"%s","log_gnss_offset_ns":%s,%s})" "\n",
              index, static_cast<unsigned>(contents->system_id), sortie ? static_cast<long long>(*sortie) : -1LL,
              pairs.size(), contents->gnss_times.size(), timed_by(timing), offset_text.c_str(),
              fit_fields(fit).c_str());
  if (records) {
    print_log_records(index, *contents, timing);
  }
  return true;
}

bool check(const Vehicles& vehicles, const timealign::Injection& injection,
           const timealign::Withholding& withholding) {
  Tally tally;
  // Not a structured binding: CodeQL drops the body of a range-for that
  // declares one over this map, and with it every call the body makes.
  for (const auto& entry : vehicles) {
    const unsigned system = entry.first;
    const Vehicle& vehicle = entry.second;
    for (std::size_t index = 0; index < vehicle.sorties.size(); ++index) {
      const Sortie& sortie = vehicle.sorties[index];
      std::vector<std::int64_t> boots;
      std::ranges::transform(sortie.positions, std::back_inserter(boots), &Positioned::boot_us);
      const Result<timealign::DriftCheck> result =
          timealign::check_injected_drift(sortie.samples, boots, injection, withholding);
      const std::optional<Outcome> outcome = result ? std::optional(timealign::judge(*result)) : std::nullopt;
      tally.checked += outcome && *outcome != Outcome::kDrifting ? 1U : 0U;
      tally.drifting += outcome == Outcome::kDrifting ? 1U : 0U;
      tally.passed = tally.passed && outcome != Outcome::kBeyond;
      print_check(system, index, boots.size(), result, outcome);
    }
  }
  const bool verdict = tally.checked > 0 && tally.passed;
  std::printf(R"({"kind":"verdict","checked":%zu,"drifting":%zu,"limit_ns":%lld,"passed":%s})" "\n",
              tally.checked, tally.drifting, static_cast<long long>(timealign::kAlignmentLimit.count()),
              verdict ? "true" : "false");
  return verdict;
}

}  // namespace ics::time_align
