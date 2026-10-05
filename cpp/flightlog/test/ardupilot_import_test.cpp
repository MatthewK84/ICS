// The importer's ArduPilot half on DataFlash logs written for each case.
#include <sys/mman.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "dataflash_builder.hpp"
#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/flightlog/dataflash.hpp"
#include "ics/flightlog/gps_time.hpp"
#include "ics/flightlog/import.hpp"
#include "import_support.hpp"

namespace ics::flightlog {
namespace {

using testing::body;
using testing::DataFlashBuilder;
using testing::geoid;
using testing::imported;

constexpr std::uint8_t kPos = 1;
constexpr std::uint8_t kXkf1 = 2;
constexpr std::uint8_t kNkf1 = 3;
constexpr std::uint8_t kAtt = 4;
constexpr std::uint8_t kGps = 5;
constexpr std::uint8_t kGpa = 6;
constexpr std::uint8_t kMsg = 7;
constexpr std::uint8_t kMode = 8;
constexpr std::uint8_t kArm = 9;
constexpr std::uint8_t kParm = 10;
// The first boot time past the latest ICS takes.
constexpr std::uint64_t kLate = 4'102'444'800'000'001;

struct Fmt {
  std::uint8_t type = 0;
  std::uint8_t length = 0;
  std::string_view name;
  std::string_view format;
  std::string_view columns;
};

// The layouts ArduCopter 4 logs, cut to the columns the importer reads.
constexpr Fmt kPosFmt{kPos, 23, "POS", "QLLf", "TimeUS,Lat,Lng,Alt"};
constexpr Fmt kXkf1Fmt{kXkf1, 24, "XKF1", "QBfff", "TimeUS,C,VN,VE,VD"};
constexpr Fmt kAttFmt{kAtt, 23, "ATT", "Qfff", "TimeUS,Roll,Pitch,Yaw"};
constexpr Fmt kGpsFmt{kGps, 43, "GPS", "QBBIHLLffff", "TimeUS,I,Status,GMS,GWk,Lat,Lng,Alt,Spd,GCrs,VZ"};
constexpr Fmt kGpaFmt{kGpa, 20, "GPA", "QBff", "TimeUS,I,HAcc,VAcc"};
constexpr Fmt kMsgFmt{kMsg, 75, "MSG", "QZ", "TimeUS,Message"};
constexpr Fmt kModeFmt{kMode, 12, "MODE", "QM", "TimeUS,Mode"};
constexpr Fmt kArmFmt{kArm, 12, "ARM", "QB", "TimeUS,ArmState"};
constexpr Fmt kParmFmt{kParm, 31, "PARM", "QNf", "TimeUS,Name,Value"};

DataFlashBuilder formatted(const std::initializer_list<Fmt> formats) {
  DataFlashBuilder log;
  for (const Fmt& fmt : formats) {
    log.format(fmt.type, fmt.length, fmt.name, fmt.format, fmt.columns);
  }
  return log;
}

std::int32_t e7(const double degrees) { return static_cast<std::int32_t>(std::lround(degrees * 1e7)); }

std::vector<std::byte> join(std::vector<std::byte> first, const std::vector<std::byte>& second) {
  first.insert(first.end(), second.begin(), second.end());
  return first;
}

std::vector<std::byte> text(const std::string_view value, const std::size_t size) {
  std::vector<std::byte> out;
  DataFlashBuilder::put_text(out, value, size);
  return out;
}

void pos(DataFlashBuilder& log, const std::uint64_t time, const double lat, const double lng, const float alt) {
  log.message(kPos, body(time, e7(lat), e7(lng), alt));
}

void xkf1(DataFlashBuilder& log, const std::uint64_t time, const std::uint8_t core, const float north) {
  log.message(kXkf1, body(time, core, north, 2.0F, 3.0F));
}

void att(DataFlashBuilder& log, const std::uint64_t time, const float yaw) {
  log.message(kAtt, body(time, 0.0F, 0.0F, yaw));
}

struct GpsMessage {
  std::uint64_t time = 0;
  std::uint8_t instance = 0;
  std::uint8_t status = 3;
  std::uint32_t ms = 0;
  std::uint16_t week = 0;
  float speed = 0.0F;
  float course = 0.0F;
  float vz = 0.0F;
};

void gps(DataFlashBuilder& log, const GpsMessage& m) {
  log.message(kGps, body(m.time, m.instance, m.status, m.ms, m.week, e7(40.001), e7(-100.002), 750.0F, m.speed,
                         m.course, m.vz));
}

void gpa(DataFlashBuilder& log, const std::uint64_t time, const std::uint8_t instance, const float horizontal,
         const float vertical) {
  log.message(kGpa, body(time, instance, horizontal, vertical));
}

void msg(DataFlashBuilder& log, const std::uint64_t time, const std::string_view message) {
  log.message(kMsg, join(body(time), text(message, 64)));
}

void parm(DataFlashBuilder& log, const std::string_view name, const float value) {
  log.message(kParm, join(join(body(std::uint64_t{0}), text(name, 16)), body(value)));
}

std::vector<std::string> details(const LogContents& contents) {
  std::vector<std::string> out;
  for (const LogEvent& event : contents.events) {
    out.push_back(v1::PliEvent::Kind_Name(event.event.kind()) + " " + event.event.detail());
  }
  return out;
}

void expect_empty(const LogContents& contents) {
  EXPECT_EQ(contents.system_id, 1U);
  EXPECT_FALSE(contents.timed);
  EXPECT_TRUE(contents.states.empty());
  EXPECT_TRUE(contents.gnss.empty());
  EXPECT_TRUE(contents.events.empty());
}

TEST(ArduPilotImport, ImportsStatesFixesAndEventsTimedByGpsWeek) {
  DataFlashBuilder log =
      formatted({kPosFmt, kXkf1Fmt, kAttFmt, kGpsFmt, kGpaFmt, kMsgFmt, kModeFmt, kArmFmt, kParmFmt});
  parm(log, "LOG_BITMASK", 3.0F);
  parm(log, "SYSID_THISMAV", 2.0F);
  parm(log, "MAV_SYSID", 9.0F);
  msg(log, 1'000, "ArduCopter V4.6.0");
  xkf1(log, 1'900'000, 0, 1.0F);
  xkf1(log, 1'900'000, 1, 9.0F);
  att(log, 1'950'000, 90.0F);
  gps(log, {.time = 2'000'000, .status = 6, .ms = 100'000, .week = 2400, .speed = 5.0F, .course = 90.0F, .vz = -1.0F});
  gps(log, {.time = 2'000'000, .instance = 1, .ms = 100'000, .week = 2400});
  gpa(log, 2'000'000, 0, 1.5F, 2.5F);
  gpa(log, 2'000'000, 1, 7.0F, 7.0F);
  pos(log, 2'000'000, 40.001, -100.002, 750.0F);
  log.message(kMode, body(std::uint64_t{2'100'000}, std::uint8_t{5}));
  log.message(kMode, body(std::uint64_t{2'150'000}, std::uint8_t{99}));
  log.message(kArm, body(std::uint64_t{2'200'000}, std::uint8_t{1}));
  log.message(kArm, body(std::uint64_t{2'300'000}, std::uint8_t{0}));
  const LogContents contents = imported(log.bytes());
  EXPECT_EQ(contents.system_id, 2U);
  EXPECT_TRUE(contents.timed);
  EXPECT_EQ(contents.counts.gnss_times, 1U);
  EXPECT_EQ(contents.counts.beyond_leap_table, 0U);
  EXPECT_EQ(contents.counts.unplaced, 0U);
  const std::int64_t utc_ns = to_utc_ns(utc_from_gps(2400, 100'000).value().utc);
  ASSERT_EQ(contents.states.size(), 1U);
  const v1::PliRecord& state = contents.states[0].record;
  EXPECT_EQ(state.valid_utc_ns(), utc_ns);
  EXPECT_EQ(state.time_basis(), v1::PLI_TIME_BASIS_VEHICLE_GNSS);
  EXPECT_EQ(state.entity_id(), "2");
  EXPECT_EQ(state.role(), v1::ENTITY_ROLE_TARGET);
  EXPECT_EQ(state.source(), v1::PLI_SOURCE_DATAFLASH);
  EXPECT_EQ(state.fix_type(), v1::PliRecord::FIX_TYPE_OTHER);
  EXPECT_EQ(state.position().latitude_deg(), e7(40.001) * 1e-7);
  EXPECT_EQ(state.position().height_ellipsoid_m(),
            geoid().from_msl(Degrees(e7(40.001) * 1e-7), Degrees(e7(-100.002) * 1e-7), Meters(750.0))->height().value());
  EXPECT_NEAR(state.velocity_enu_mps().north(), 1.0, 1e-3);
  EXPECT_NEAR(state.velocity_enu_mps().east(), 2.0, 1e-3);
  EXPECT_NEAR(state.velocity_enu_mps().up(), -3.0, 1e-3);
  EXPECT_NEAR(state.attitude().w(), std::sqrt(0.5), 1e-12);
  EXPECT_NEAR(state.attitude().z(), std::sqrt(0.5), 1e-12);
  ASSERT_EQ(contents.gnss.size(), 1U);
  const v1::PliRecord& fix = contents.gnss[0].record;
  EXPECT_EQ(fix.fix_type(), v1::PliRecord::FIX_TYPE_RTK_FIXED);
  EXPECT_EQ(fix.valid_utc_ns(), utc_ns);
  EXPECT_EQ(fix.horizontal_sigma_m(), 1.5);
  EXPECT_EQ(fix.vertical_sigma_m(), 2.5);
  EXPECT_NEAR(fix.velocity_enu_mps().north(), 0.0, 1e-3);
  EXPECT_NEAR(fix.velocity_enu_mps().east(), 5.0, 1e-3);
  EXPECT_NEAR(fix.velocity_enu_mps().up(), 1.0, 1e-3);
  EXPECT_FALSE(fix.has_attitude());
  EXPECT_EQ(details(contents),
            (std::vector<std::string>{"KIND_STATUS_TEXT ArduCopter V4.6.0", "KIND_MODE_CHANGED LOITER",
                                      "KIND_MODE_CHANGED mode 99", "KIND_ARMED ", "KIND_DISARMED "}));
  EXPECT_EQ(contents.events[1].event.time_utc_ns(), utc_ns + 100'000'000);
}

TEST(ArduPilotImport, TakesAnyLogWithoutTheULogHeaderAsDataFlash) {
  expect_empty(imported(std::vector<std::byte>{}));
  expect_empty(imported(std::vector<std::byte>{std::byte{0x55}, std::byte{0x4c}}));
}

TEST(ArduPilotImport, FailsAsTheReaderDoes) {
  const std::size_t size = DataFlash::kMaxBytes + 1;
  void* const mapping = mmap(nullptr, size, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
  ASSERT_NE(mapping, MAP_FAILED);
  EXPECT_EQ(testing::try_import(std::span<const std::byte>(static_cast<const std::byte*>(mapping), size)).error(),
            Error::kInvalidArgument);
  munmap(mapping, size);
}

TEST(ArduPilotImport, LeavesOutTypesMissingAColumn) {
  DataFlashBuilder log;
  log.format(kPos, 15, "POS", "LLf", "Lat,Lng,Alt")
      .format(kAtt, 19, "ATT", "Qff", "TimeUS,Roll,Pitch")
      .format(kMsg, 11, "MSG", "Q", "TimeUS")
      .format(kMode, 11, "MODE", "Q", "TimeUS")
      .format(kParm, 27, "PARM", "QN", "TimeUS,Name");
  log.message(kPos, body(e7(40.0), e7(-100.0), 700.0F));
  log.message(kAtt, body(std::uint64_t{1'000}, 0.0F, 0.0F));
  log.message(kMsg, body(std::uint64_t{1'000}));
  log.message(kMode, body(std::uint64_t{1'000}));
  log.message(kParm, join(body(std::uint64_t{0}), text("MAV_SYSID", 16)));
  const LogContents contents = imported(log.bytes());
  expect_empty(contents);
  EXPECT_EQ(contents.counts.unplaced, 0U);
}

TEST(ArduPilotImport, FallsBackToEkf2AndKeepsFixesWithoutVelocity) {
  DataFlashBuilder log = formatted({kPosFmt});
  log.format(kNkf1, 23, "NKF1", "Qfff", "TimeUS,VN,VE,VD")
      .format(kGps, 31, "GPS", "QBBIHLLf", "TimeUS,I,Status,GMS,GWk,Lat,Lng,Alt")
      .format(kMsg, 67, "MSG", "Z", "Message")
      .format(kParm, 15, "PARM", "Qf", "TimeUS,Value");
  log.message(kNkf1, body(std::uint64_t{1'000}, 4.0F, 0.0F, 0.0F));
  pos(log, 1'500, 40.0, -100.0, 700.0F);
  log.message(kGps, body(std::uint64_t{1'500}, std::uint8_t{0}, std::uint8_t{3}, std::uint32_t{0}, std::uint16_t{0},
                         e7(40.0), e7(-100.0), 700.0F));
  log.message(kMsg, text("untimed", 64));
  log.message(kParm, body(std::uint64_t{0}, 7.0F));
  const LogContents contents = imported(log.bytes());
  EXPECT_EQ(contents.system_id, 1U);
  EXPECT_FALSE(contents.timed);
  ASSERT_EQ(contents.states.size(), 1U);
  EXPECT_NEAR(contents.states[0].record.velocity_enu_mps().north(), 4.0, 1e-9);
  EXPECT_FALSE(contents.states[0].record.has_attitude());
  ASSERT_EQ(contents.gnss.size(), 1U);
  EXPECT_FALSE(contents.gnss[0].record.has_velocity_enu_mps());
  EXPECT_FALSE(contents.gnss[0].record.has_horizontal_sigma_m());
  EXPECT_TRUE(contents.events.empty());
}

TEST(ArduPilotImport, SkipsMessagesWithoutABootTimeOrValueItCanUse) {
  DataFlashBuilder log = formatted({kPosFmt, kXkf1Fmt, kAttFmt, kGpsFmt, kGpaFmt, kMsgFmt});
  // A MODE whose TimeUS is text, and an ARM whose ArmState is.
  log.format(kMode, 8, "MODE", "nM", "TimeUS,Mode").format(kArm, 15, "ARM", "qn", "TimeUS,ArmState");
  pos(log, kLate, 40.0, -100.0, 700.0F);
  xkf1(log, kLate, 0, 1.0F);
  att(log, kLate, 0.0F);
  gps(log, {.time = kLate, .week = 2400});
  gpa(log, kLate, 0, 1.0F, 1.0F);
  msg(log, kLate, "late");
  log.message(kMode, join(text("1000", 4), body(std::uint8_t{5})));
  log.message(kArm, join(body(std::int64_t{-5}), text("ON", 4)));
  log.message(kArm, join(body(std::int64_t{5}), text("ON", 4)));
  pos(log, 1'000, 95.0, -100.0, 700.0F);
  const LogContents contents = imported(log.bytes());
  EXPECT_FALSE(contents.timed);
  EXPECT_TRUE(contents.states.empty());
  EXPECT_TRUE(contents.gnss.empty());
  EXPECT_TRUE(contents.events.empty());
  EXPECT_EQ(contents.counts.unplaced, 3U);
}

void float_gps(DataFlashBuilder& log, const std::uint64_t time, const float status, const float ms, const float week) {
  log.message(kGps, body(time, std::uint8_t{0}, status, ms, week, e7(40.0), e7(-100.0), 700.0F));
}

TEST(ArduPilotImport, ChecksFloatColumnsAreWholeNumbersInRange) {
  DataFlashBuilder log = formatted({kGpaFmt, kParmFmt});
  log.format(kGps, 36, "GPS", "QBfffLLf", "TimeUS,I,Status,GMS,GWk,Lat,Lng,Alt")
      .format(kMsg, 15, "MSG", "Qf", "TimeUS,Message")
      .format(kMode, 15, "MODE", "Qf", "TimeUS,Mode");
  float_gps(log, 1'000, 3.0F, 0.0F, 1.5F);
  float_gps(log, 2'000, 3.0F, -1.0F, 2400.0F);
  float_gps(log, 3'000, 3.0F, 0.0F, 0.0F);
  float_gps(log, 4'000, 3.0F, 7e8F, 2400.0F);
  float_gps(log, 5'000, 3.0F, 0.0F, 5e9F);
  float_gps(log, 6'000, 2.0F, 0.0F, 2500.0F);
  float_gps(log, 7'000, 300.0F, 0.0F, 2400.0F);
  float_gps(log, 8'000, 1.0F, 0.0F, 0.0F);
  gpa(log, 1'000, 0, std::numeric_limits<float>::quiet_NaN(), 1.0F);
  gpa(log, 2'000, 0, -1.0F, -1.0F);
  gpa(log, 6'000, 0, 1.0F, 2.0F);
  log.message(kMsg, body(std::uint64_t{10}, 1.0F));
  log.message(kMode, body(std::uint64_t{20}, 5.0F));
  parm(log, "MAV_SYSID", 300.0F);
  const LogContents contents = imported(log.bytes());
  EXPECT_EQ(contents.system_id, 1U);
  EXPECT_EQ(contents.counts.gnss_times, 2U);
  EXPECT_EQ(contents.counts.beyond_leap_table, 1U);
  EXPECT_EQ(contents.counts.unplaced, 2U);
  ASSERT_EQ(contents.gnss.size(), 6U);
  EXPECT_FALSE(contents.gnss[0].record.has_horizontal_sigma_m());
  EXPECT_EQ(contents.gnss[0].record.vertical_sigma_m(), 1.0);
  EXPECT_FALSE(contents.gnss[1].record.has_horizontal_sigma_m());
  EXPECT_FALSE(contents.gnss[1].record.has_vertical_sigma_m());
  EXPECT_EQ(contents.gnss[5].record.fix_type(), v1::PliRecord::FIX_TYPE_TWO_DIMENSIONAL);
  EXPECT_EQ(contents.gnss[5].record.horizontal_sigma_m(), 1.0);
  EXPECT_FALSE(contents.gnss[5].record.has_vertical_sigma_m());
  EXPECT_EQ(details(contents), (std::vector<std::string>{"KIND_MODE_CHANGED mode 5"}));
}

TEST(ArduPilotImport, NamesOnlyWholeCopterModeNumbers) {
  DataFlashBuilder log = formatted({kMsgFmt, kParmFmt});
  log.format(kMode, 15, "MODE", "Qf", "TimeUS,Mode");
  msg(log, 10, "ArduCopter V4.6.0");
  log.message(kMode, body(std::uint64_t{20}, 2.5F));
  log.message(kMode, body(std::uint64_t{30}, 3.0F));
  parm(log, "MAV_SYSID", 0.0F);
  const LogContents contents = imported(log.bytes());
  EXPECT_EQ(contents.system_id, 1U);
  EXPECT_EQ(details(contents), (std::vector<std::string>{"KIND_STATUS_TEXT ArduCopter V4.6.0",
                                                         "KIND_MODE_CHANGED mode 2.5", "KIND_MODE_CHANGED AUTO"}));
}

TEST(ArduPilotImport, JoinsOnlyAVelocityAndAttitudeNoOlderThanTheLimit) {
  DataFlashBuilder log = formatted({kPosFmt, kXkf1Fmt, kAttFmt});
  pos(log, 1'000, 40.0, -100.0, 700.0F);
  xkf1(log, 2'000, 0, 1.0F);
  att(log, 2'000, 0.0F);
  pos(log, 3'000, 40.0, -100.0, 700.0F);
  pos(log, 1'002'000, 40.0, -100.0, 700.0F);
  pos(log, 1'002'001, 40.0, -100.0, 700.0F);
  // Times that go back, as after a damaged stretch.
  pos(log, 1'500, 40.0, -100.0, 700.0F);
  pos(log, 2'500, 40.0, -100.0, 700.0F);
  const LogContents contents = imported(log.bytes());
  ASSERT_EQ(contents.states.size(), 6U);
  const std::vector<bool> expected{false, true, true, false, false, true};
  for (std::size_t i = 0; i < expected.size(); ++i) {
    EXPECT_EQ(contents.states[i].record.has_velocity_enu_mps(), expected[i]) << i;
    EXPECT_EQ(contents.states[i].record.has_attitude(), expected[i]) << i;
  }
}

}  // namespace
}  // namespace ics::flightlog
