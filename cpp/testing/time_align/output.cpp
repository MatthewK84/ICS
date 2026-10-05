#include "output.hpp"

#include <algorithm>
#include <array>
#include <chrono>
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
using timealign::ClockSample;

constexpr Duration kLimit = std::chrono::milliseconds(1);

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
      R"("fitted":true,"used":%zu,"rejected":%zu,"origin_boot_us":%lld,"origin_utc_ns":%lld,"drift_ppm":%.6f,)"
      R"("residual_rms_ns":%.0f,"residual_max_ns":%.0f,"first_boot_us":%lld,"last_boot_us":%lld)",
      fit->used, fit->rejected, static_cast<long long>(fit->model.origin_boot_us()),
      static_cast<long long>(to_utc_ns(fit->model.origin_utc())), fit->model.drift_ppm(), fit->residual_rms.count(),
      fit->residual_max.count(), static_cast<long long>(fit->first_boot_us), static_cast<long long>(fit->last_boot_us));
  return std::string(text.data(), static_cast<std::size_t>(std::max(written, 0)));
}

void add_latencies(const Sortie& sortie, const timealign::ClockModel& model, std::vector<Duration>& out) {
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

void print_records(const unsigned system, const std::size_t index, const Sortie& sortie, const Result<ClockFit>& fit) {
  for (const Positioned& position : sortie.positions) {
    const std::optional<v1::PliRecord> timed =
        fit ? timealign::aligned(position.record, position.boot_us, fit->model) : std::nullopt;
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

void print_log_records(const std::size_t index, const flightlog::LogContents& contents, const Result<ClockFit>& fit) {
  const auto print = [index, &fit](const char* kind, const flightlog::LogRecord& logged) {
    const std::optional<v1::PliRecord> timed =
        fit ? timealign::aligned(logged.record, logged.boot_us, fit->model) : std::nullopt;
    std::printf(R"({"kind":"%s","log":%zu,"boot_us":%lld,"record":%s})" "\n", kind, index,
                static_cast<long long>(logged.boot_us), json(timed ? *timed : logged.record).c_str());
  };
  std::ranges::for_each(contents.states, [&print](const flightlog::LogRecord& r) { print("log_record", r); });
  std::ranges::for_each(contents.gnss, [&print](const flightlog::LogRecord& r) { print("log_record", r); });
  for (const flightlog::LogEvent& logged : contents.events) {
    const std::optional<v1::PliEvent> timed =
        fit ? timealign::aligned(logged.event, logged.boot_us, fit->model) : std::nullopt;
    std::printf(R"({"kind":"log_event","log":%zu,"boot_us":%lld,"event":%s})" "\n", index,
                static_cast<long long>(logged.boot_us), json(timed ? *timed : logged.event).c_str());
  }
}

}  // namespace

void report(const Vehicles& vehicles, const bool records) {
  for (const auto& [system, vehicle] : vehicles) {
    std::vector<Duration> latencies;
    for (std::size_t index = 0; index < vehicle.sorties.size(); ++index) {
      const Sortie& sortie = vehicle.sorties[index];
      const Result<ClockFit> fit = timealign::fit_clock(sortie.samples);
      std::printf(R"({"kind":"sortie","system":%u,"sortie":%zu,"samples":%zu,"positions":%zu,%s})" "\n",
                  unsigned{system}, index, sortie.samples.size(), sortie.positions.size(), fit_fields(fit).c_str());
      if (fit) {
        add_latencies(sortie, fit->model, latencies);
      }
      if (records) {
        print_records(system, index, sortie, fit);
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
  std::vector<ClockSample> samples =
      sortie ? vehicles.at(static_cast<std::uint8_t>(contents->system_id)).sorties[*sortie].samples
             : std::vector<ClockSample>();
  for (const flightlog::GnssTime& time : contents->gnss_times) {
    samples.push_back(ClockSample{.boot_us = time.boot_us, .utc_ns = to_utc_ns(time.utc)});
  }
  std::ranges::sort(samples, {}, &ClockSample::boot_us);
  const Result<ClockFit> fit = timealign::fit_clock(samples);
  std::printf(R"({"kind":"log","log":%zu,"system":%u,"sortie":%lld,"samples":%zu,"log_gnss_times":%zu,%s})" "\n",
              index, static_cast<unsigned>(contents->system_id), sortie ? static_cast<long long>(*sortie) : -1LL,
              samples.size(), contents->gnss_times.size(), fit_fields(fit).c_str());
  if (records) {
    print_log_records(index, *contents, fit);
  }
  return true;
}

bool check(const Vehicles& vehicles, const timealign::Injection& injection,
           const timealign::Withholding& withholding) {
  std::size_t checked = 0;
  bool passed = true;
  for (const auto& [system, vehicle] : vehicles) {
    for (std::size_t index = 0; index < vehicle.sorties.size(); ++index) {
      const Sortie& sortie = vehicle.sorties[index];
      std::vector<std::int64_t> boots;
      std::ranges::transform(sortie.positions, std::back_inserter(boots), &Positioned::boot_us);
      const Result<timealign::DriftCheck> result =
          timealign::check_injected_drift(sortie.samples, boots, injection, withholding);
      const bool ok = result && result->max_error <= kLimit;
      checked += result ? 1U : 0U;
      passed = passed && (ok || !result);
      std::printf(R"({"kind":"check","system":%u,"sortie":%zu,"positions":%zu,"checked":%s,"max_error_ns":%lld,)"
                  R"("reference_spread_ns":%.0f,"passed":%s,%s})" "\n",
                  unsigned{system}, index, boots.size(), result ? "true" : "false",
                  result ? static_cast<long long>(result->max_error.count()) : -1LL,
                  result ? result->reference_spread.count() : -1.0, ok ? "true" : "false",
                  fit_fields(result ? Result<ClockFit>(result->fit) : fail(result.error())).c_str());
    }
  }
  const bool verdict = checked > 0 && passed;
  std::printf(R"({"kind":"verdict","checked":%zu,"limit_ns":%lld,"passed":%s})" "\n", checked,
              static_cast<long long>(kLimit.count()), verdict ? "true" : "false");
  return verdict;
}

}  // namespace ics::time_align
