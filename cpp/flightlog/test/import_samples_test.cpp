// The ICS-025 "Done when": full-rate states extracted from the sample logs
// (logs/README.md). Every estimated position and every GNSS fix pyulog or
// pymavlink counted (logs/expected.tsv) becomes a record, timed from the
// log's GNSS time where it has one.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "fixtures.hpp"
#include "import_support.hpp"
#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/flightlog/gps_time.hpp"
#include "ics/flightlog/import.hpp"
#include "ics/frames/egm96.hpp"
#include "ics/frames/enu.hpp"
#include "ics/frames/geodetic.hpp"

namespace ics::flightlog {
namespace {

using testing::expected;
using testing::Expected;
using testing::geoid;

LogContents import(const std::string& name) { return testing::imported(testing::log_file(name)); }

std::vector<v1::PliEvent::Kind> kinds(const LogContents& contents, const bool text) {
  std::vector<v1::PliEvent::Kind> out;
  for (const LogEvent& event : contents.events) {
    if ((event.event.kind() == v1::PliEvent::KIND_STATUS_TEXT) == text) {
      out.push_back(event.event.kind());
    }
  }
  return out;
}

TEST(ImportSamples, ExtractsEveryStateOfTheSitlPx4LogUntimed) {
  const std::string log = "px4-crossing.ulg";
  const LogContents contents = import(log);
  EXPECT_FALSE(contents.timed);
  EXPECT_TRUE(contents.gnss_times.empty());
  ASSERT_EQ(contents.states.size(), std::stoul(testing::expected_value(log, "valid_global")));
  EXPECT_EQ(contents.gnss.size(), std::stoul(testing::expected_value(log, "fixed_gps")));
  EXPECT_EQ(contents.counts.gnss_times, 0U);
  const Expected first = expected(log, "first_global").at(0);
  const v1::PliRecord& state = contents.states.front().record;
  EXPECT_EQ(contents.states.front().boot_us, std::stoll(first.named("timestamp")));
  EXPECT_EQ(state.time_basis(), v1::PLI_TIME_BASIS_UNSPECIFIED);
  EXPECT_EQ(state.valid_utc_ns(), 0);
  EXPECT_EQ(state.entity_id(), "1");
  EXPECT_EQ(state.role(), v1::ENTITY_ROLE_OTHER);
  EXPECT_EQ(state.source(), v1::PLI_SOURCE_ULOG);
  EXPECT_EQ(state.fix_type(), v1::PliRecord::FIX_TYPE_OTHER);
  EXPECT_EQ(state.position().latitude_deg(), first.number("lat"));
  EXPECT_EQ(state.position().longitude_deg(), first.number("lon"));
  EXPECT_EQ(state.position().height_ellipsoid_m(), first.number("alt_ellipsoid"));
  EXPECT_EQ(state.horizontal_sigma_m(), first.number("eph"));
  EXPECT_TRUE(state.has_velocity_enu_mps());
  EXPECT_TRUE(state.has_attitude());
  EXPECT_EQ(contents.gnss.front().record.fix_type(), v1::PliRecord::FIX_TYPE_THREE_DIMENSIONAL);
  // Navigation states 4 then 3, arming at 4.132 s, and 19 logged strings.
  EXPECT_EQ(kinds(contents, false), (std::vector{v1::PliEvent::KIND_MODE_CHANGED, v1::PliEvent::KIND_MODE_CHANGED,
                                                 v1::PliEvent::KIND_ARMED}));
  EXPECT_EQ(contents.events.front().event.detail(), "AUTO_LOITER");
  EXPECT_EQ(kinds(contents, true).size(), std::stoul(testing::expected_value(log, "strings")));
}

TEST(ImportSamples, TimesPx4StatesByTheReceiversUtc) {
  const std::string log = "pyulog-sample-px4-events.ulg";
  const LogContents contents = import(log);
  EXPECT_TRUE(contents.timed);
  EXPECT_EQ(contents.counts.gnss_times, std::stoul(testing::expected_value(log, "timed_gps")));
  ASSERT_EQ(contents.states.size(), std::stoul(testing::expected_value(log, "valid_global")));
  EXPECT_EQ(contents.gnss.size(), std::stoul(testing::expected_value(log, "fixed_gps")));
  // This simulator's boot clock is UTC, so each offset is 0.
  const LogRecord& first = contents.states.front();
  EXPECT_EQ(first.record.time_basis(), v1::PLI_TIME_BASIS_VEHICLE_GNSS);
  EXPECT_EQ(first.record.valid_utc_ns(), first.boot_us * 1000);
  const Expected gps = expected(log, "first_gps").at(0);
  EXPECT_EQ(contents.gnss.front().record.valid_utc_ns(), std::stoll(gps.named("time_utc_usec")) * 1000);
  EXPECT_EQ(contents.gnss.front().record.fix_type(), v1::PliRecord::FIX_TYPE_TWO_DIMENSIONAL);
  ASSERT_EQ(contents.gnss_times.size(), contents.counts.gnss_times);
  EXPECT_TRUE(std::ranges::is_sorted(contents.gnss_times, {}, &GnssTime::boot_us));
  EXPECT_FALSE(contents.gnss.front().record.has_vertical_sigma_m());
  EXPECT_EQ(kinds(contents, false),
            (std::vector{v1::PliEvent::KIND_MODE_CHANGED, v1::PliEvent::KIND_ARMED, v1::PliEvent::KIND_MODE_CHANGED,
                         v1::PliEvent::KIND_MODE_CHANGED, v1::PliEvent::KIND_MODE_CHANGED,
                         v1::PliEvent::KIND_DISARMED, v1::PliEvent::KIND_MODE_CHANGED}));
}

TEST(ImportSamples, ReadsOlderPx4GnssInIntegerUnits) {
  const std::string log = "pyulog-sample-logging-tagged.ulg";
  const LogContents contents = import(log);
  EXPECT_TRUE(contents.timed);
  EXPECT_TRUE(contents.states.empty());
  ASSERT_EQ(contents.gnss.size(), std::stoul(testing::expected_value(log, "fixed_gps")));
  const Expected gps = expected(log, "first_gps").at(0);
  const v1::PliRecord& fix = contents.gnss.front().record;
  EXPECT_EQ(fix.position().latitude_deg(), gps.number("lat") * 1e-7);
  EXPECT_EQ(fix.position().longitude_deg(), gps.number("lon") * 1e-7);
  EXPECT_EQ(fix.position().height_ellipsoid_m(), gps.number("alt_ellipsoid") * 1e-3);
  EXPECT_EQ(fix.valid_utc_ns(), std::stoll(gps.named("time_utc_usec")) * 1000);
  EXPECT_EQ(kinds(contents, true).size(), std::stoul(testing::expected_value(log, "strings")));
}

TEST(ImportSamples, ReadsAPx4LogWithAppendedData) {
  const LogContents contents = import("pyulog-sample-appended-multiple.ulg");
  EXPECT_FALSE(contents.timed);
  EXPECT_TRUE(contents.states.empty());
  EXPECT_EQ(kinds(contents, false), (std::vector{v1::PliEvent::KIND_MODE_CHANGED}));
  // A logged string 0.12 s before the first status.
  ASSERT_EQ(contents.events.size(), 2U);
  EXPECT_EQ(contents.events[0].event.kind(), v1::PliEvent::KIND_STATUS_TEXT);
  EXPECT_EQ(contents.events[1].event.detail(), "MANUAL");
}

TEST(ImportSamples, ExtractsEveryStateOfTheSitlArduCopterLogTimedByGpsWeek) {
  const std::string log = "ardupilot-crossing.bin";
  const LogContents contents = import(log);
  EXPECT_TRUE(contents.timed);
  EXPECT_EQ(contents.system_id, 2U);
  EXPECT_EQ(contents.counts.gnss_times, std::stoul(testing::expected_value(log, "timed_gps")));
  ASSERT_EQ(contents.states.size(), testing::expected_counts(log).at("POS"));
  EXPECT_EQ(contents.gnss.size(), std::stoul(testing::expected_value(log, "fixed_gps")));
  const Expected pos = expected(log, "first_pos").at(0);
  const Expected gps = expected(log, "first_timed_gps").at(0);
  const LogRecord& first = contents.states.front();
  const std::int64_t gps_utc_ns =
      to_utc_ns(utc_from_gps(static_cast<std::uint32_t>(gps.number("GWk")), static_cast<std::uint32_t>(gps.number("GMS")))
                    .value()
                    .utc);
  EXPECT_EQ(first.boot_us, std::stoll(pos.named("TimeUS")));
  EXPECT_EQ(first.record.valid_utc_ns(), gps_utc_ns + ((first.boot_us - std::stoll(gps.named("TimeUS"))) * 1000));
  ASSERT_EQ(contents.gnss_times.size(), contents.counts.gnss_times);
  EXPECT_EQ(contents.gnss_times.front().boot_us, std::stoll(gps.named("TimeUS")));
  EXPECT_EQ(to_utc_ns(contents.gnss_times.front().utc), gps_utc_ns);
  EXPECT_EQ(first.record.entity_id(), "2");
  EXPECT_EQ(first.record.role(), v1::ENTITY_ROLE_TARGET);
  EXPECT_EQ(first.record.source(), v1::PLI_SOURCE_DATAFLASH);
  const frames::Geodetic expected_point =
      geoid().from_msl(Degrees(pos.number("Lat")), Degrees(pos.number("Lng")), Meters(pos.number("Alt"))).value();
  EXPECT_EQ(first.record.position().latitude_deg(), pos.number("Lat"));
  EXPECT_EQ(first.record.position().height_ellipsoid_m(), expected_point.height().value());
  EXPECT_EQ(contents.gnss.front().record.fix_type(), v1::PliRecord::FIX_TYPE_RTK_FIXED);
  EXPECT_EQ(kinds(contents, true).size(), std::stoul(testing::expected_value(log, "strings")));
  std::vector<std::string> modes;
  for (const LogEvent& event : contents.events) {
    if (event.event.kind() == v1::PliEvent::KIND_MODE_CHANGED) {
      modes.push_back(event.event.detail());
    }
  }
  EXPECT_EQ(modes, (std::vector<std::string>{"GUIDED", "GUIDED", "AUTO"}));
}

}  // namespace
}  // namespace ics::flightlog
