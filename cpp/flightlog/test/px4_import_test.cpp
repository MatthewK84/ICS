// The importer's PX4 half on ULog logs written for each case.
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "ics/common/units.hpp"
#include "ics/flightlog/import.hpp"
#include "import_support.hpp"
#include "ulog_builder.hpp"

namespace ics::flightlog {
namespace {

using testing::geoid;
using testing::imported;
using testing::sample;
using testing::ULogBuilder;

constexpr std::uint16_t kGlobalId = 1;
constexpr std::uint16_t kLocalId = 2;
constexpr std::uint16_t kAttitudeId = 3;
constexpr std::uint16_t kGpsId = 4;
constexpr std::uint16_t kStatusId = 5;
// The first boot time past the latest ICS takes.
constexpr std::uint64_t kLate = 4'102'444'800'000'001;
// 2023-11-14T22:13:20Z.
constexpr std::uint64_t kUtcUs = 1'700'000'000'000'000;

// The layouts PX4 v1.17 logs, cut to the fields the importer reads.
constexpr std::string_view kGlobal =
    "vehicle_global_position:uint64_t timestamp;double lat;double lon;float alt;float alt_ellipsoid;float eph;"
    "float epv;bool lat_lon_valid;bool alt_valid;";
constexpr std::string_view kLocal =
    "vehicle_local_position:uint64_t timestamp;float vx;float vy;float vz;bool v_xy_valid;bool v_z_valid;";
constexpr std::string_view kAttitude = "vehicle_attitude:uint64_t timestamp;float[4] q;";
constexpr std::string_view kSensorGps =
    "sensor_gps:uint64_t timestamp;uint64_t time_utc_usec;int32_t timestamp_time_relative;double latitude_deg;"
    "double longitude_deg;double altitude_ellipsoid_m;uint8_t fix_type;float eph;float epv;float vel_n_m_s;"
    "float vel_e_m_s;float vel_d_m_s;bool vel_ned_valid;";
constexpr std::string_view kStatus = "vehicle_status:uint64_t timestamp;uint8_t arming_state;uint8_t nav_state;";

struct Topic {
  std::uint16_t id = 0;
  std::string_view format;
};

// Defines each topic and subscribes to its instance 0.
void define(ULogBuilder& log, const std::initializer_list<Topic> topics) {
  for (const Topic& topic : topics) {
    log.format(topic.format);
  }
  for (const Topic& topic : topics) {
    log.add_logged(0, topic.id, topic.format.substr(0, topic.format.find(':')));
  }
}

void define_v117(ULogBuilder& log) {
  define(log, {{kGlobalId, kGlobal},
               {kLocalId, kLocal},
               {kAttitudeId, kAttitude},
               {kGpsId, kSensorGps},
               {kStatusId, kStatus}});
}

struct Global {
  std::uint64_t time = 0;
  double lat = 40.001;
  bool lat_lon_valid = true;
  bool alt_valid = true;
};

std::vector<std::byte> global(const Global& g) {
  return sample(g.time, g.lat, -100.002, 720.0F, 700.0F, 0.5F, 0.75F, static_cast<std::uint8_t>(g.lat_lon_valid),
                static_cast<std::uint8_t>(g.alt_valid));
}

std::vector<std::byte> local(const std::uint64_t time, const bool xy_valid, const bool z_valid) {
  return sample(time, 1.0F, 2.0F, 3.0F, static_cast<std::uint8_t>(xy_valid), static_cast<std::uint8_t>(z_valid));
}

struct Gnss {
  std::uint64_t time = 0;
  std::uint64_t utc_us = 0;
  std::int32_t relative = 0;
  double lat = 40.001;
  std::uint8_t fix = 3;
  bool velocity_valid = true;
};

std::vector<std::byte> gnss(const Gnss& g) {
  return sample(g.time, g.utc_us, g.relative, g.lat, -100.002, 700.0, g.fix, 1.25F, 2.5F, 1.0F, 2.0F, 3.0F,
                static_cast<std::uint8_t>(g.velocity_valid));
}

std::vector<std::byte> status(const std::uint64_t time, const std::uint8_t arming, const std::uint8_t nav) {
  return sample(time, arming, nav);
}

// The first bytes of a sample.
std::vector<std::byte> cut(std::vector<std::byte> bytes, const std::size_t size) {
  bytes.resize(size);
  return bytes;
}

std::vector<std::string> details(const LogContents& contents) {
  std::vector<std::string> out;
  for (const LogEvent& event : contents.events) {
    out.push_back(v1::PliEvent::Kind_Name(event.event.kind()) + " " + event.event.detail());
  }
  return out;
}

TEST(Px4Import, ImportsStatesFixesAndEventsTimedByTheReceiversUtc) {
  ULogBuilder log;
  log.key_value('P', "int32_t MAV_SYS_ID", sample(std::int32_t{2}));
  define_v117(log);
  log.logging(100, "Takeoff detected");
  log.data(kStatusId, status(500'000, 1, 3))
      .data(kStatusId, status(600'000, 2, 3))
      .data(kStatusId, status(700'000, 1, 40))
      .data(kStatusId, status(800'000, 1, 7))
      .data(kLocalId, local(900'000, true, true))
      .data(kAttitudeId, sample(std::uint64_t{950'000}, 0.5F, 0.5F, 0.5F, 0.5F))
      .data(kGpsId, gnss({.time = 1'000'000, .utc_us = kUtcUs, .relative = -1'000}))
      .data(kGlobalId, global({.time = 1'000'000}));
  const LogContents contents = imported(log.bytes());
  EXPECT_EQ(contents.system_id, 2U);
  EXPECT_TRUE(contents.timed);
  EXPECT_EQ(contents.counts.gnss_times, 1U);
  EXPECT_EQ(contents.counts.unplaced, 0U);
  // The receiver's time is of 999'000 us since boot.
  const std::int64_t utc_ns = (static_cast<std::int64_t>(kUtcUs) + 1'000) * 1'000;
  ASSERT_EQ(contents.states.size(), 1U);
  const v1::PliRecord& state = contents.states[0].record;
  EXPECT_EQ(contents.states[0].boot_us, 1'000'000);
  EXPECT_EQ(state.valid_utc_ns(), utc_ns);
  EXPECT_EQ(state.entity_id(), "2");
  EXPECT_EQ(state.role(), v1::ENTITY_ROLE_TARGET);
  EXPECT_EQ(state.source(), v1::PLI_SOURCE_ULOG);
  EXPECT_EQ(state.position().latitude_deg(), 40.001);
  EXPECT_EQ(state.position().longitude_deg(), -100.002);
  EXPECT_EQ(state.position().height_ellipsoid_m(), 700.0);
  EXPECT_EQ(state.horizontal_sigma_m(), 0.5);
  EXPECT_EQ(state.vertical_sigma_m(), 0.75);
  EXPECT_NEAR(state.velocity_enu_mps().north(), 1.0, 1e-3);
  EXPECT_NEAR(state.velocity_enu_mps().east(), 2.0, 1e-3);
  EXPECT_NEAR(state.velocity_enu_mps().up(), -3.0, 1e-3);
  EXPECT_EQ(state.attitude().w(), 0.5);
  EXPECT_EQ(state.attitude().z(), 0.5);
  ASSERT_EQ(contents.gnss.size(), 1U);
  const v1::PliRecord& fix = contents.gnss[0].record;
  EXPECT_EQ(fix.valid_utc_ns(), utc_ns);
  EXPECT_EQ(fix.fix_type(), v1::PliRecord::FIX_TYPE_THREE_DIMENSIONAL);
  EXPECT_EQ(fix.horizontal_sigma_m(), 1.25);
  EXPECT_EQ(fix.vertical_sigma_m(), 2.5);
  EXPECT_NEAR(fix.velocity_enu_mps().up(), -3.0, 1e-3);
  EXPECT_EQ(details(contents),
            (std::vector<std::string>{"KIND_STATUS_TEXT Takeoff detected", "KIND_MODE_CHANGED AUTO_MISSION",
                                      "KIND_ARMED ", "KIND_DISARMED ", "KIND_MODE_CHANGED nav_state 40",
                                      "KIND_MODE_CHANGED nav_state 7"}));
  EXPECT_EQ(contents.events[0].event.time_utc_ns(), utc_ns - ((1'000'000 - 100) * 1'000));
}

TEST(Px4Import, ImportsAnEmptyLogAndFailsAsTheReaderDoes) {
  const LogContents contents = imported(ULogBuilder().bytes());
  EXPECT_EQ(contents.system_id, 1U);
  EXPECT_FALSE(contents.timed);
  EXPECT_TRUE(contents.states.empty());
  EXPECT_TRUE(contents.gnss.empty());
  EXPECT_TRUE(contents.events.empty());
  const std::vector<std::byte> header = cut(ULogBuilder().bytes(), 7);
  EXPECT_FALSE(testing::try_import(header).has_value());
}

TEST(Px4Import, LeavesOutSamplesThatAreInvalidOrUnreadable) {
  ULogBuilder log;
  define_v117(log);
  log.logging(kLate, "late");
  log.data(kStatusId, cut(status(1'000, 1, 3), 9))
      .data(kLocalId, local(1'000, false, true))
      .data(kLocalId, local(1'000, true, false))
      .data(kLocalId, cut(local(1'000, true, true), 8))
      .data(kAttitudeId, cut(sample(std::uint64_t{1'000}, 1.0F, 0.0F, 0.0F, 0.0F), 8))
      .data(kGlobalId, global({.time = 2'000, .lat_lon_valid = false}))
      .data(kGlobalId, global({.time = 2'000, .alt_valid = false}))
      .data(kGlobalId, global({.time = 2'000, .lat = 95.0}))
      .data(kGlobalId, cut(global({.time = 2'000}), 12))
      .data(kGlobalId, cut(global({.time = 2'000}), 4))
      .data(kGlobalId, global({.time = kLate}))
      .data(kGpsId, gnss({.time = 3'000}))
      .data(kGpsId, gnss({.time = 3'000, .utc_us = 4'102'444'800'000'000}))
      .data(kGpsId, gnss({.time = 500, .utc_us = kUtcUs, .relative = -1'000}))
      .data(kGpsId, gnss({.time = kLate - 1, .utc_us = kUtcUs, .relative = 1}))
      .data(kGpsId, gnss({.time = 3'000, .velocity_valid = false}))
      .data(kGpsId, gnss({.time = 3'000, .fix = 1}))
      .data(kGpsId, cut(gnss({.time = 3'000}), 8));
  const LogContents contents = imported(log.bytes());
  EXPECT_FALSE(contents.timed);
  EXPECT_EQ(contents.counts.gnss_times, 0U);
  EXPECT_EQ(contents.counts.unplaced, 7U);
  ASSERT_EQ(contents.states.size(), 1U);
  const v1::PliRecord& state = contents.states[0].record;
  EXPECT_EQ(state.horizontal_sigma_m(), 0.5);
  EXPECT_FALSE(state.has_vertical_sigma_m());
  EXPECT_FALSE(state.has_velocity_enu_mps());
  EXPECT_FALSE(state.has_attitude());
  ASSERT_EQ(contents.gnss.size(), 5U);
  EXPECT_TRUE(contents.gnss[0].record.has_velocity_enu_mps());
  EXPECT_FALSE(contents.gnss[4].record.has_velocity_enu_mps());
  EXPECT_TRUE(contents.events.empty());
}

TEST(Px4Import, ReadsAHeightAboveMeanSeaLevelAndLeavesOutTopicsMissingAField) {
  ULogBuilder log;
  define(log, {{kGlobalId, "vehicle_global_position:uint64_t timestamp;double lat;double lon;float alt;"},
               {kLocalId, "vehicle_local_position:uint64_t timestamp;float vx;float vy;"},
               {kAttitudeId, "vehicle_attitude:uint64_t timestamp;float roll;"},
               {kGpsId, "vehicle_gps_position:uint64_t timestamp;int32_t lat;int32_t lon;int32_t alt_ellipsoid;"},
               {kStatusId, "vehicle_status:uint64_t timestamp;uint8_t arming_state;"}});
  log.data(kLocalId, sample(std::uint64_t{500}, 1.0F, 2.0F))
      .data(kAttitudeId, sample(std::uint64_t{500}, 1.0F))
      .data(kGpsId, sample(std::uint64_t{500}, std::int32_t{400'000'000}, std::int32_t{-1'000'000'000}, std::int32_t{0}))
      .data(kStatusId, sample(std::uint64_t{500}, std::uint8_t{2}))
      .data(kGlobalId, sample(std::uint64_t{1'000}, 40.0, -100.0, 720.0F));
  const LogContents contents = imported(log.bytes());
  EXPECT_FALSE(contents.timed);
  ASSERT_EQ(contents.states.size(), 1U);
  const v1::PliRecord& state = contents.states[0].record;
  EXPECT_EQ(state.position().height_ellipsoid_m(),
            geoid().from_msl(Degrees(40.0), Degrees(-100.0), Meters(720.0))->height().value());
  EXPECT_FALSE(state.has_horizontal_sigma_m());
  EXPECT_FALSE(state.has_velocity_enu_mps());
  EXPECT_FALSE(state.has_attitude());
  EXPECT_TRUE(contents.gnss.empty());
  EXPECT_TRUE(contents.events.empty());
}

TEST(Px4Import, TakesTopicsWithoutATimestampAsMissingAndChecksFloatStates) {
  ULogBuilder log;
  define(log, {{kGlobalId, "vehicle_global_position:double lat;double lon;float alt_ellipsoid;"},
               {kGpsId,
                "sensor_gps:uint64_t timestamp;double latitude_deg;double longitude_deg;double altitude_ellipsoid_m;"
                "float fix_type;"},
               {kStatusId, "vehicle_status:uint64_t timestamp;uint8_t arming_state;float nav_state;"}});
  log.data(kGlobalId, sample(40.0, -100.0, 700.0F))
      .data(kGpsId, sample(std::uint64_t{1'000}, 40.0, -100.0, 700.0, 3.0F))
      .data(kGpsId, sample(std::uint64_t{2'000}, 40.0, -100.0, 700.0, 300.0F))
      .data(kStatusId, sample(std::uint64_t{3'000}, std::uint8_t{1}, -1.0F))
      .data(kStatusId, sample(std::uint64_t{4'000}, std::uint8_t{1}, -1.0F))
      .data(kStatusId, sample(std::uint64_t{5'000}, std::uint8_t{1}, 2.0F));
  const LogContents contents = imported(log.bytes());
  EXPECT_TRUE(contents.states.empty());
  EXPECT_EQ(contents.counts.unplaced, 1U);
  ASSERT_EQ(contents.gnss.size(), 1U);
  EXPECT_FALSE(contents.gnss[0].record.has_velocity_enu_mps());
  EXPECT_FALSE(contents.gnss[0].record.has_horizontal_sigma_m());
  EXPECT_EQ(details(contents),
            (std::vector<std::string>{"KIND_MODE_CHANGED nav_state -1", "KIND_MODE_CHANGED POSCTL"}));
}

}  // namespace
}  // namespace ics::flightlog
