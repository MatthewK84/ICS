// The importer's PX4 half (import.hpp): records and events from a ULog log.
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ics/common/check.hpp"
#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/flightlog/import.hpp"
#include "ics/flightlog/ulog.hpp"
#include "ics/frames/egm96.hpp"
#include "ics/frames/enu.hpp"
#include "ics/frames/geodetic.hpp"
#include "ics/mavlink/conversions.hpp"
#include "importing.hpp"

namespace ics::flightlog {
namespace {

using detail::BootClock;
using detail::Context;
using detail::Latest;
using detail::Ned;
using detail::Quaternion;
using detail::Reading;

constexpr double kDegreesE7 = 1e-7;
constexpr double kMillimetres = 1e-3;
constexpr std::int64_t kNsPerUs = 1'000;
constexpr double kLatestUtcUs = static_cast<double>(detail::kLatestUtcUs);
constexpr double kMaxRelativeUs = 2'147'483'647.0;  // INT32_MAX
constexpr double kArmed = 2.0;  // vehicle_status ARMING_STATE_ARMED
constexpr double kMinimumFix = 2.0;

// PX4's navigation states, as vehicle_status in v1.17 names them.
constexpr std::array<std::string_view, 31> kNavStates{
    "MANUAL",          "ALTCTL",          "POSCTL",       "AUTO_MISSION",       "AUTO_LOITER",   "AUTO_RTL",
    "POSITION_SLOW",   "",                "ALTITUDE_CRUISE", "",                "ACRO",          "",
    "DESCEND",         "TERMINATION",     "OFFBOARD",     "STAB",               "",              "AUTO_TAKEOFF",
    "AUTO_LAND",       "AUTO_FOLLOW_TARGET", "AUTO_PRECLAND", "ORBIT",          "AUTO_VTOL_TAKEOFF", "EXTERNAL1",
    "EXTERNAL2",       "EXTERNAL3",       "EXTERNAL4",    "EXTERNAL5",          "EXTERNAL6",     "EXTERNAL7",
    "EXTERNAL8"};

[[nodiscard]] std::string nav_state_name(const double nav_state) {
  const bool in_table = nav_state >= 0.0 && nav_state < static_cast<double>(kNavStates.size());
  const std::string_view name = in_table ? kNavStates.at(static_cast<std::size_t>(nav_state)) : std::string_view();
  return name.empty() ? std::format("nav_state {}", nav_state) : std::string(name);
}

// Instance 0 of a topic, with its format.
struct Topic {
  ULogFormat format;
  std::vector<std::span<const std::byte>> samples;
};

[[nodiscard]] std::optional<Topic> topic(const ULog& log, const std::string_view name) {
  const auto format = log.format(name);
  if (!format) {
    return std::nullopt;
  }
  return Topic{.format = format->get(), .samples = log.samples(name, 0)};
}

// A field to read: its name, which element, and the scale to its unit.
struct Wanted {
  std::string_view name;
  std::size_t index = 0;
  double scale = 1.0;
};

// Reads a sample's timestamp and up to kMaxValues of its fields, all or
// nothing.
class Fields {
 public:
  // Nothing when the topic lacks the timestamp or one of the fields.
  [[nodiscard]] static std::optional<Fields> make(const Topic& topic, const std::initializer_list<Wanted> wanted) {
    static_cast<void>(check(wanted.size() <= detail::kMaxValues));
    Fields out;
    const std::optional<ULogField> timestamp = find_field(topic.format, "timestamp");
    if (!timestamp) {
      return std::nullopt;
    }
    out.timestamp_ = *timestamp;
    for (const Wanted& one : wanted) {
      const std::optional<ULogField> field = find_field(topic.format, one.name);
      if (!field) {
        return std::nullopt;
      }
      out.fields_.emplace_back(*field, one);
    }
    return out;
  }

  // Nothing when a sample is too short for a field, or its timestamp is past
  // the latest boot time ICS takes.
  [[nodiscard]] std::optional<Reading> read(const std::span<const std::byte> sample) const {
    const std::optional<std::int64_t> time = read_integer(timestamp_, sample);
    if (!time || *time > detail::kMaxBootUs) {
      return std::nullopt;
    }
    Reading out{.boot_us = *time};
    std::size_t next = 0;
    for (const auto& [field, wanted] : fields_) {
      const std::optional<double> value = flightlog::read(field, sample, wanted.index);
      if (!value) {
        return std::nullopt;
      }
      out.values.at(next) = *value * wanted.scale;
      ++next;
    }
    return out;
  }

 private:
  ULogField timestamp_;
  std::vector<std::pair<ULogField, Wanted>> fields_;
};

// An optional field: a flag or a sigma some layouts lack.
[[nodiscard]] std::optional<double> optional_value(const Topic& topic, const std::string_view name,
                                                   const std::span<const std::byte> sample) {
  const std::optional<ULogField> field = find_field(topic.format, name);
  return field ? read(*field, sample) : std::nullopt;
}

// Whether an optional validity flag holds: true when the layout has none.
[[nodiscard]] bool holds(const Topic& topic, const std::string_view flag, const std::span<const std::byte> sample) {
  return optional_value(topic, flag, sample).value_or(1.0) != 0.0;
}

[[nodiscard]] std::optional<Topic> gnss_topic(const ULog& log) {
  std::optional<Topic> gps = topic(log, "vehicle_gps_position");
  return gps ? gps : topic(log, "sensor_gps");
}

// The boot time of a GNSS sample's UTC time, timestamp +
// timestamp_time_relative, as sensor_gps defines it, if it is one ICS takes.
// The offset is an int32 there, so a larger one is not an offset.
[[nodiscard]] std::optional<std::int64_t> receiver_boot_us(const Reading& reading) {
  const double relative_us = reading.values[1];
  if (!(std::abs(relative_us) <= kMaxRelativeUs)) {
    return std::nullopt;
  }
  const std::int64_t boot_us = reading.boot_us + static_cast<std::int64_t>(relative_us);
  if (boot_us < 0 || boot_us > detail::kMaxBootUs) {
    return std::nullopt;
  }
  return boot_us;
}

// Each GNSS sample with a UTC time: the receiver's time, at its boot time.
void read_clock(const ULog& log, BootClock& clock, ImportCounts& counts) {
  const std::optional<Topic> gps = gnss_topic(log);
  const auto fields = gps ? Fields::make(*gps, {{"time_utc_usec"}, {"timestamp_time_relative"}}) : std::nullopt;
  for (const std::span<const std::byte> sample : fields ? gps->samples : std::vector<std::span<const std::byte>>()) {
    const std::optional<Reading> reading = fields->read(sample);
    const double utc_us = reading ? reading->values[0] : 0.0;
    const std::optional<std::int64_t> boot_us = reading ? receiver_boot_us(*reading) : std::nullopt;
    if (boot_us && utc_us > 0.0 && utc_us < kLatestUtcUs) {
      clock.add(*boot_us, utc_from_ns(static_cast<std::int64_t>(utc_us) * kNsPerUs));
      ++counts.gnss_times;
    }
  }
}

[[nodiscard]] std::vector<std::pair<std::int64_t, Ned>> velocities(const ULog& log) {
  std::vector<std::pair<std::int64_t, Ned>> out;
  const std::optional<Topic> local = topic(log, "vehicle_local_position");
  const auto fields = local ? Fields::make(*local, {{"vx"}, {"vy"}, {"vz"}}) : std::nullopt;
  for (const std::span<const std::byte> sample : fields ? local->samples : std::vector<std::span<const std::byte>>()) {
    const std::optional<Reading> v = fields->read(sample);
    if (v && holds(*local, "v_xy_valid", sample) && holds(*local, "v_z_valid", sample)) {
      out.emplace_back(v->boot_us, Ned{.north = v->values[0], .east = v->values[1], .down = v->values[2]});
    }
  }
  return out;
}

[[nodiscard]] std::vector<std::pair<std::int64_t, Quaternion>> attitudes(const ULog& log) {
  std::vector<std::pair<std::int64_t, Quaternion>> out;
  const std::optional<Topic> attitude = topic(log, "vehicle_attitude");
  const auto fields = attitude ? Fields::make(*attitude, {{"q", 0}, {"q", 1}, {"q", 2}, {"q", 3}}) : std::nullopt;
  for (const std::span<const std::byte> sample : fields ? attitude->samples : std::vector<std::span<const std::byte>>()) {
    const std::optional<Reading> q = fields->read(sample);
    if (q) {
      out.emplace_back(q->boot_us, Quaternion{.w = q->values[0], .x = q->values[1], .y = q->values[2], .z = q->values[3]});
    }
  }
  return out;
}

// A record's point: latitude and longitude in degrees, and a height above
// the ellipsoid, or above mean sea level.
[[nodiscard]] Result<frames::Geodetic> place(const Context& context, const std::array<double, detail::kMaxValues>& values,
                                             const bool ellipsoid) {
  const Degrees latitude(values[0]);
  const Degrees longitude(values[1]);
  return ellipsoid ? frames::Geodetic::make(latitude, longitude, Meters(values[2]))
                   : context.geoid.from_msl(latitude, longitude, Meters(values[2]));
}

[[nodiscard]] std::vector<LogRecord> states(const ULog& log, const Context& context, ImportCounts& counts) {
  std::vector<LogRecord> out;
  const std::optional<Topic> global = topic(log, "vehicle_global_position");
  const bool ellipsoid = global && find_field(global->format, "alt_ellipsoid");
  const auto fields =
      global ? Fields::make(*global, {{"lat"}, {"lon"}, {ellipsoid ? "alt_ellipsoid" : "alt"}}) : std::nullopt;
  const Latest<Ned> velocity(velocities(log), context.settings.max_age);
  const Latest<Quaternion> attitude(attitudes(log), context.settings.max_age);
  for (const std::span<const std::byte> sample : fields ? global->samples : std::vector<std::span<const std::byte>>()) {
    const std::optional<Reading> reading = fields->read(sample);
    const Result<frames::Geodetic> where = reading ? place(context, reading->values, ellipsoid) : fail(Error::kMalformed);
    if (!where || !holds(*global, "lat_lon_valid", sample)) {
      ++counts.unplaced;
      continue;
    }
    v1::PliRecord record = detail::record(context, *where, v1::PliRecord::FIX_TYPE_OTHER);
    const bool height = holds(*global, "alt_valid", sample);
    detail::set_sigmas(record, optional_value(*global, "eph", sample),
                       height ? optional_value(*global, "epv", sample) : std::nullopt);
    detail::set_motion(record, velocity.at(reading->boot_us), attitude.at(reading->boot_us), *where, context);
    out.push_back(LogRecord{.record = std::move(record), .boot_us = reading->boot_us});
  }
  return out;
}

// sensor_gps's position, fix type, and a field telling its layout: degrees
// and metres since PX4 v1.14, 1e-7 degrees and millimetres before.
[[nodiscard]] std::optional<Fields> gnss_fields(const Topic& gps) {
  const bool degrees = find_field(gps.format, "latitude_deg").has_value();
  if (degrees) {
    return Fields::make(gps, {{"latitude_deg"}, {"longitude_deg"}, {"altitude_ellipsoid_m"}, {"fix_type"}});
  }
  return Fields::make(gps, {{"lat", 0, kDegreesE7}, {"lon", 0, kDegreesE7}, {"alt_ellipsoid", 0, kMillimetres},
                                {"fix_type"}});
}

[[nodiscard]] std::optional<Ned> gnss_velocity(const Topic& gps, const std::optional<Fields>& fields,
                                               const std::span<const std::byte> sample) {
  const std::optional<Reading> v = fields ? fields->read(sample) : std::nullopt;
  if (!v || !holds(gps, "vel_ned_valid", sample)) {
    return std::nullopt;
  }
  return Ned{.north = v->values[0], .east = v->values[1], .down = v->values[2]};
}

[[nodiscard]] std::vector<LogRecord> fixes(const ULog& log, const Context& context, ImportCounts& counts) {
  std::vector<LogRecord> out;
  const std::optional<Topic> gps = gnss_topic(log);
  const std::optional<Fields> fields = gps ? gnss_fields(*gps) : std::nullopt;
  const auto motion = gps ? Fields::make(*gps, {{"vel_n_m_s"}, {"vel_e_m_s"}, {"vel_d_m_s"}}) : std::nullopt;
  for (const std::span<const std::byte> sample : fields ? gps->samples : std::vector<std::span<const std::byte>>()) {
    const std::optional<Reading> reading = fields->read(sample);
    const double fix = reading ? reading->values[3] : 0.0;
    const Result<frames::Geodetic> where =
        fix >= kMinimumFix && fix <= 255.0 ? place(context, reading->values, true)
                                           : fail(Error::kMalformed);
    if (!where) {
      ++counts.unplaced;
      continue;
    }
    v1::PliRecord record = detail::record(context, *where, mavlink::fix_type(static_cast<std::uint8_t>(fix)));
    detail::set_sigmas(record, optional_value(*gps, "eph", sample),
                       fix > kMinimumFix ? optional_value(*gps, "epv", sample) : std::nullopt);
    detail::set_motion(record, gnss_velocity(*gps, motion, sample), std::nullopt, *where, context);
    out.push_back(LogRecord{.record = std::move(record), .boot_us = reading->boot_us});
  }
  return out;
}

// Arming and navigation state changes. A vehicle counts as disarmed, in no
// mode, before its first status.
void status_events(const ULog& log, const Context& context, std::vector<LogEvent>& out) {
  const std::optional<Topic> status = topic(log, "vehicle_status");
  const auto fields = status ? Fields::make(*status, {{"arming_state"}, {"nav_state"}}) : std::nullopt;
  bool armed = false;
  std::optional<double> mode;
  for (const std::span<const std::byte> sample : fields ? status->samples : std::vector<std::span<const std::byte>>()) {
    const std::optional<Reading> reading = fields->read(sample);
    if (!reading) {
      continue;
    }
    const bool now_armed = reading->values[0] == kArmed;
    if (now_armed != armed) {
      const v1::PliEvent::Kind kind = now_armed ? v1::PliEvent::KIND_ARMED : v1::PliEvent::KIND_DISARMED;
      out.push_back(LogEvent{.event = detail::event(context, kind, {}), .boot_us = reading->boot_us});
    }
    if (mode != reading->values[1]) {
      out.push_back(LogEvent{.event = detail::event(context, v1::PliEvent::KIND_MODE_CHANGED,
                                                    nav_state_name(reading->values[1])),
                             .boot_us = reading->boot_us});
    }
    armed = now_armed;
    mode = reading->values[1];
  }
}

void text_events(const ULog& log, const Context& context, std::vector<LogEvent>& out) {
  for (const ULogString& text : log.strings()) {
    if (text.timestamp_us <= static_cast<std::uint64_t>(detail::kMaxBootUs)) {
      out.push_back(LogEvent{
          .event = detail::event(context, v1::PliEvent::KIND_STATUS_TEXT, mavlink::printable(text.text)),
          .boot_us = static_cast<std::int64_t>(text.timestamp_us)});
    }
  }
}

[[nodiscard]] std::uint32_t system_id(const ULog& log) {
  return detail::system_id(log.parameter("MAV_SYS_ID"));
}

}  // namespace

Result<LogContents> import_ulog(const std::span<const std::byte> log, const ImportSettings& settings,
                                const frames::Egm96& geoid, const frames::EnuFrame& range) {
  const Result<ULog> parsed = ULog::parse(log);
  if (!parsed) {
    return fail(parsed.error());
  }
  LogContents out;
  out.system_id = system_id(*parsed);
  const Context context{.settings = settings,
                        .geoid = geoid,
                        .range = range,
                        .vehicle = detail::vehicle(out.system_id, settings, v1::PLI_SOURCE_ULOG)};
  BootClock clock;
  read_clock(*parsed, clock, out.counts);
  out.states = states(*parsed, context, out.counts);
  out.gnss = fixes(*parsed, context, out.counts);
  status_events(*parsed, context, out.events);
  text_events(*parsed, context, out.events);
  detail::stamp(out, clock);
  return out;
}

}  // namespace ics::flightlog
