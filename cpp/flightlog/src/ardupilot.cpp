// The importer's ArduPilot half (import.hpp): records and events from a
// DataFlash log.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <initializer_list>
#include <map>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ics/common/check.hpp"
#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/flightlog/dataflash.hpp"
#include "ics/flightlog/gps_time.hpp"
#include "ics/flightlog/import.hpp"
#include "ics/frames/egm96.hpp"
#include "ics/frames/enu.hpp"
#include "ics/frames/geodetic.hpp"
#include "ics/mavlink/conversions.hpp"
#include "ics/mavlink/modes.hpp"
#include "importing.hpp"

namespace ics::flightlog {
namespace {

using detail::BootClock;
using detail::Context;
using detail::Latest;
using detail::Ned;
using detail::Quaternion;
using detail::Reading;

using Messages = std::vector<std::span<const std::byte>>;

constexpr double kMinimumFix = 2.0;
constexpr double kThreeDimensional = 3.0;
constexpr double kMaxWhole = 4'294'967'295.0;  // UINT32_MAX
constexpr std::int64_t kNsPerUs = 1'000;

// One type of message, with its format.
struct Type {
  DataFlashFormat format;
  Messages messages;
};

// A type's messages; only its first instance's when it has an instance
// column, such as GPS's I.
[[nodiscard]] std::optional<Type> type(const DataFlash& log, const std::string_view name,
                                       const std::optional<std::string_view> instance = std::nullopt) {
  const auto format = log.format(name);
  if (!format) {
    return std::nullopt;
  }
  Type out{.format = format->get(), .messages = {}};
  const std::optional<DataFlashColumn> column = instance ? find_column(out.format, *instance) : std::nullopt;
  for (const std::span<const std::byte> message : log.messages(name)) {
    if (!column || read(*column, message) == 0.0) {
      out.messages.push_back(message);
    }
  }
  return out;
}

// A message's TimeUS, when it is a boot time ICS takes.
[[nodiscard]] std::optional<std::int64_t> boot_time(const DataFlashColumn& time, const std::span<const std::byte> message) {
  const std::optional<double> value = read(time, message);
  if (!value || !(*value >= 0.0 && *value <= static_cast<double>(detail::kMaxBootUs))) {
    return std::nullopt;
  }
  return static_cast<std::int64_t>(*value);
}

// Reads a message's TimeUS and up to kMaxValues of its columns, all or
// nothing.
class Columns {
 public:
  // Nothing when the type lacks TimeUS or one of the columns.
  [[nodiscard]] static std::optional<Columns> make(const Type& type,
                                                   const std::initializer_list<std::string_view> names) {
    static_cast<void>(check(names.size() <= detail::kMaxValues));
    Columns out;
    const std::optional<DataFlashColumn> time = find_column(type.format, "TimeUS");
    if (!time) {
      return std::nullopt;
    }
    out.time_ = *time;
    for (const std::string_view name : names) {
      std::optional<DataFlashColumn> column = find_column(type.format, name);
      if (!column) {
        return std::nullopt;
      }
      out.columns_.push_back(std::move(*column));
    }
    return out;
  }

  // Nothing when a column is not a number, or TimeUS is not a boot time ICS
  // takes.
  [[nodiscard]] std::optional<Reading> read(const std::span<const std::byte> message) const {
    const std::optional<std::int64_t> time = boot_time(time_, message);
    if (!time) {
      return std::nullopt;
    }
    Reading out{.boot_us = *time};
    std::size_t next = 0;
    for (const DataFlashColumn& column : columns_) {
      const std::optional<double> value = flightlog::read(column, message);
      if (!value) {
        return std::nullopt;
      }
      out.values.at(next) = *value;
      ++next;
    }
    return out;
  }

 private:
  DataFlashColumn time_;
  std::vector<DataFlashColumn> columns_;
};

// A type's messages and a reader of their columns: no messages when the type
// or a column is missing.
struct Readable {
  Messages messages;
  std::optional<Columns> columns;
};

[[nodiscard]] Readable readable(const std::optional<Type>& type, const std::initializer_list<std::string_view> names) {
  std::optional<Columns> columns = type ? Columns::make(*type, names) : std::nullopt;
  return Readable{.messages = columns ? type->messages : Messages(), .columns = std::move(columns)};
}

// Whether a value is a whole number ICS can take as a uint32.
[[nodiscard]] bool is_whole(const double value) noexcept {
  return value >= 0.0 && value <= kMaxWhole && std::trunc(value) == value;
}

// Each first-instance GPS message with a time: its week and milliseconds,
// in UTC. A week of 0 is no time.
void read_clock(const DataFlash& log, BootClock& clock, ImportCounts& counts) {
  const Readable gps = readable(type(log, "GPS", "I"), {"GWk", "GMS"});
  for (const std::span<const std::byte> message : gps.messages) {
    const std::optional<Reading> reading = gps.columns->read(message);
    const double week = reading ? reading->values[0] : 0.0;
    const double ms = reading ? reading->values[1] : 0.0;
    const Result<GpsUtc> utc = week >= 1.0 && is_whole(week) && is_whole(ms)
                                   ? utc_from_gps(static_cast<std::uint32_t>(week), static_cast<std::uint32_t>(ms))
                                   : fail(Error::kInvalidArgument);
    if (utc && to_utc_ns(utc->utc) < detail::kLatestUtcUs * kNsPerUs) {
      clock.add(reading->boot_us, utc->utc);
      ++counts.gnss_times;
      counts.beyond_leap_table += utc->beyond_table ? 1U : 0U;
    }
  }
}

// The primary EKF's velocities: EKF3's core 0 (XKF1), or EKF2's (NKF1).
[[nodiscard]] std::vector<std::pair<std::int64_t, Ned>> velocities(const DataFlash& log) {
  std::vector<std::pair<std::int64_t, Ned>> out;
  std::optional<Type> ekf = type(log, "XKF1", "C");
  const Readable v = readable(ekf ? ekf : type(log, "NKF1", "C"), {"VN", "VE", "VD"});
  for (const std::span<const std::byte> message : v.messages) {
    const std::optional<Reading> reading = v.columns->read(message);
    if (reading) {
      out.emplace_back(reading->boot_us,
                       Ned{.north = reading->values[0], .east = reading->values[1], .down = reading->values[2]});
    }
  }
  return out;
}

// Roll, pitch and yaw in degrees, as the body to NED rotation (ZYX).
[[nodiscard]] Quaternion quaternion(const std::array<double, detail::kMaxValues>& degrees) {
  constexpr double half = std::numbers::pi / 360.0;
  const double cr = std::cos(degrees[0] * half);
  const double sr = std::sin(degrees[0] * half);
  const double cp = std::cos(degrees[1] * half);
  const double sp = std::sin(degrees[1] * half);
  const double cy = std::cos(degrees[2] * half);
  const double sy = std::sin(degrees[2] * half);
  return Quaternion{.w = (cr * cp * cy) + (sr * sp * sy),
                    .x = (sr * cp * cy) - (cr * sp * sy),
                    .y = (cr * sp * cy) + (sr * cp * sy),
                    .z = (cr * cp * sy) - (sr * sp * cy)};
}

[[nodiscard]] std::vector<std::pair<std::int64_t, Quaternion>> attitudes(const DataFlash& log) {
  std::vector<std::pair<std::int64_t, Quaternion>> out;
  const Readable att = readable(type(log, "ATT"), {"Roll", "Pitch", "Yaw"});
  for (const std::span<const std::byte> message : att.messages) {
    const std::optional<Reading> reading = att.columns->read(message);
    if (reading) {
      out.emplace_back(reading->boot_us, quaternion(reading->values));
    }
  }
  return out;
}

// A point from a latitude and longitude, and a height above mean sea level.
[[nodiscard]] Result<frames::Geodetic> place(const Context& context, const double latitude, const double longitude,
                                             const double msl) {
  return context.geoid.from_msl(Degrees(latitude), Degrees(longitude), Meters(msl));
}

[[nodiscard]] std::vector<LogRecord> states(const DataFlash& log, const Context& context, ImportCounts& counts) {
  std::vector<LogRecord> out;
  const Readable pos = readable(type(log, "POS"), {"Lat", "Lng", "Alt"});
  const Latest<Ned> velocity(velocities(log), context.settings.max_age);
  const Latest<Quaternion> attitude(attitudes(log), context.settings.max_age);
  for (const std::span<const std::byte> message : pos.messages) {
    const std::optional<Reading> reading = pos.columns->read(message);
    const Result<frames::Geodetic> where =
        reading ? place(context, reading->values[0], reading->values[1], reading->values[2])
                : fail(Error::kMalformed);
    if (!where) {
      ++counts.unplaced;
      continue;
    }
    v1::PliRecord record = detail::record(context, *where, v1::PliRecord::FIX_TYPE_OTHER);
    detail::set_motion(record, velocity.at(reading->boot_us), attitude.at(reading->boot_us), *where, context);
    out.push_back(LogRecord{.record = std::move(record), .boot_us = reading->boot_us});
  }
  return out;
}

// Each first-instance GPA message's horizontal and vertical accuracy, by its
// TimeUS, which matches the GPS message it goes with.
[[nodiscard]] std::map<std::int64_t, std::pair<double, double>> accuracies(const DataFlash& log) {
  std::map<std::int64_t, std::pair<double, double>> out;
  const Readable gpa = readable(type(log, "GPA", "I"), {"HAcc", "VAcc"});
  for (const std::span<const std::byte> message : gpa.messages) {
    const std::optional<Reading> reading = gpa.columns->read(message);
    if (reading) {
      out.insert_or_assign(reading->boot_us, std::pair{reading->values[0], reading->values[1]});
    }
  }
  return out;
}

// A GPS message's speed and course over ground, and its vertical speed,
// positive down.
[[nodiscard]] std::optional<Ned> gps_velocity(const std::optional<Columns>& columns,
                                              const std::span<const std::byte> message) {
  const std::optional<Reading> reading = columns ? columns->read(message) : std::nullopt;
  if (!reading) {
    return std::nullopt;
  }
  const double speed = reading->values[0];
  const double radians = to_radians(Degrees(reading->values[1])).value();
  return Ned{.north = speed * std::cos(radians), .east = speed * std::sin(radians), .down = reading->values[2]};
}

[[nodiscard]] std::vector<LogRecord> fixes(const DataFlash& log, const Context& context, ImportCounts& counts) {
  std::vector<LogRecord> out;
  const std::optional<Type> gps_type = type(log, "GPS", "I");
  const Readable gps = readable(gps_type, {"Status", "Lat", "Lng", "Alt"});
  const std::optional<Columns> motion = gps_type ? Columns::make(*gps_type, {"Spd", "GCrs", "VZ"}) : std::nullopt;
  const std::map<std::int64_t, std::pair<double, double>> accuracy = accuracies(log);
  for (const std::span<const std::byte> message : gps.messages) {
    const std::optional<Reading> reading = gps.columns->read(message);
    const double status = reading ? reading->values[0] : 0.0;
    const Result<frames::Geodetic> where =
        status >= kMinimumFix && status <= 255.0
            ? place(context, reading->values[1], reading->values[2], reading->values[3])
            : fail(Error::kMalformed);
    if (!where) {
      ++counts.unplaced;
      continue;
    }
    v1::PliRecord record = detail::record(context, *where, mavlink::fix_type(static_cast<std::uint8_t>(status)));
    const auto found = accuracy.find(reading->boot_us);
    const std::optional<std::pair<double, double>> sigmas =
        found == accuracy.end() ? std::nullopt : std::optional(found->second);
    detail::set_sigmas(record, sigmas ? std::optional(sigmas->first) : std::nullopt,
                       sigmas && status >= kThreeDimensional ? std::optional(sigmas->second) : std::nullopt);
    detail::set_motion(record, gps_velocity(motion, message), std::nullopt, *where, context);
    out.push_back(LogRecord{.record = std::move(record), .boot_us = reading->boot_us});
  }
  return out;
}

// Logged text, as status text.
void text_events(const DataFlash& log, const Context& context, std::vector<LogEvent>& out) {
  const std::optional<Type> msg = type(log, "MSG");
  const std::optional<DataFlashColumn> time = msg ? find_column(msg->format, "TimeUS") : std::nullopt;
  const std::optional<DataFlashColumn> column = msg ? find_column(msg->format, "Message") : std::nullopt;
  for (const std::span<const std::byte> message : time && column ? msg->messages : Messages()) {
    const std::optional<std::int64_t> boot_us = boot_time(*time, message);
    const std::optional<std::string> text = read_text(*column, message);
    if (boot_us && text) {
      out.push_back(LogEvent{.event = detail::event(context, v1::PliEvent::KIND_STATUS_TEXT, mavlink::printable(*text)),
                             .boot_us = *boot_us});
    }
  }
}

// Whether the log's text names ArduCopter, as its firmware version does.
[[nodiscard]] bool is_copter(const std::vector<LogEvent>& events) {
  return std::ranges::any_of(events, [](const LogEvent& event) { return event.event.detail().starts_with("ArduCopter"); });
}

[[nodiscard]] std::string mode_name(const double mode, const bool copter) {
  const std::optional<std::string_view> name =
      copter && is_whole(mode) ? mavlink::copter_mode_name(static_cast<std::uint32_t>(mode)) : std::nullopt;
  return name ? std::string(*name) : std::format("mode {}", mode);
}

// Mode changes and arming, as the MODE and ARM messages give them.
void state_events(const DataFlash& log, const Context& context, std::vector<LogEvent>& out) {
  const bool copter = is_copter(out);
  const Readable mode = readable(type(log, "MODE"), {"Mode"});
  for (const std::span<const std::byte> message : mode.messages) {
    const std::optional<Reading> reading = mode.columns->read(message);
    if (reading) {
      out.push_back(LogEvent{
          .event = detail::event(context, v1::PliEvent::KIND_MODE_CHANGED, mode_name(reading->values[0], copter)),
          .boot_us = reading->boot_us});
    }
  }
  const Readable arm = readable(type(log, "ARM"), {"ArmState"});
  for (const std::span<const std::byte> message : arm.messages) {
    const std::optional<Reading> reading = arm.columns->read(message);
    if (reading) {
      const v1::PliEvent::Kind kind = reading->values[0] != 0.0 ? v1::PliEvent::KIND_ARMED : v1::PliEvent::KIND_DISARMED;
      out.push_back(LogEvent{.event = detail::event(context, kind, {}), .boot_us = reading->boot_us});
    }
  }
}

// The MAVLink system ID, as first logged: MAV_SYSID since ArduPilot 4.7,
// SYSID_THISMAV before.
[[nodiscard]] std::uint32_t system_id(const DataFlash& log) {
  const std::optional<Type> parm = type(log, "PARM");
  const std::optional<DataFlashColumn> name = parm ? find_column(parm->format, "Name") : std::nullopt;
  const std::optional<DataFlashColumn> value = parm ? find_column(parm->format, "Value") : std::nullopt;
  const Messages messages = name && value ? parm->messages : Messages();
  const auto found = std::ranges::find_if(messages, [&name](const std::span<const std::byte> message) {
    const std::string text = read_text(*name, message).value_or(std::string());
    return text == "MAV_SYSID" || text == "SYSID_THISMAV";
  });
  return detail::system_id(found == messages.end() ? std::nullopt : read(*value, *found));
}

}  // namespace

Result<LogContents> import_dataflash(const std::span<const std::byte> log, const ImportSettings& settings,
                                     const frames::Egm96& geoid, const frames::EnuFrame& range) {
  const Result<DataFlash> parsed = DataFlash::parse(log);
  if (!parsed) {
    return fail(parsed.error());
  }
  LogContents out;
  out.system_id = system_id(*parsed);
  const Context context{.settings = settings,
                        .geoid = geoid,
                        .range = range,
                        .vehicle = detail::vehicle(out.system_id, settings, v1::PLI_SOURCE_DATAFLASH)};
  BootClock clock;
  read_clock(*parsed, clock, out.counts);
  out.states = states(*parsed, context, out.counts);
  out.gnss = fixes(*parsed, context, out.counts);
  text_events(*parsed, context, out.events);
  state_events(*parsed, context, out.events);
  detail::stamp(out, clock);
  return out;
}

}  // namespace ics::flightlog
