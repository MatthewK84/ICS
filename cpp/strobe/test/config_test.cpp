#include "ics/strobe/config.hpp"

#include <chrono>
#include <set>
#include <string>

#include <gtest/gtest.h>

#include "ics/config/config.hpp"
#include "ics/config/reader.hpp"
#include "ics/logging/logger.hpp"

namespace {

using std::chrono::microseconds;

TEST(StrobeConfig, ReadsTheExample) {
  const auto config = ics::config::read_file(ICS_STROBE_EXAMPLE, &ics::strobe::read_analyzer_config);
  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->log.level, ics::logging::Level::kInfo);
  EXPECT_EQ(config->log.service, "ics-strobe-analyzer");
  EXPECT_EQ(config->station_id, "station-1");
  EXPECT_EQ(config->strobe.camera_id, "phantom-1");
  EXPECT_EQ(config->offsets_file, "/var/lib/ics/camera-offsets.binpb");
  EXPECT_EQ(config->strobe.roi.x, 600U);
  EXPECT_EQ(config->strobe.roi.y, 400U);
  EXPECT_EQ(config->strobe.roi.width, 16U);
  EXPECT_EQ(config->strobe.roi.height, 16U);
  EXPECT_EQ(config->strobe.schedule.pulse_width, microseconds(20));
  EXPECT_EQ(config->strobe.schedule.latency, ics::Duration(1500));
  EXPECT_EQ(config->strobe.schedule.delay_step, microseconds(5));
  EXPECT_EQ(config->strobe.schedule.sweep_steps, 40U);
  EXPECT_EQ(config->strobe.max_offset, std::chrono::milliseconds(1));
  EXPECT_TRUE(config->strobe.schedule.valid());
}

TEST(StrobeConfig, NamesEachBadSetting) {
  const auto table = ics::config::parse(R"(
[log]
level = "info"
service = "ics-strobe-analyzer"

[strobe]
station_id = "station-1"
offsets_file = "/var/lib/ics/camera-offsets.binpb"
roi_x_px = -1
roi_y_px = 0
roi_width_px = 0
roi_height_px = 16
pulse_width_ns = 0
latency_ns = 0
delay_step_ns = 0
sweep_steps = 3_601
max_offset_ns = 0
shutter = 1
)");
  ASSERT_TRUE(table.has_value());
  const auto config = ics::config::read(*table, &ics::strobe::read_analyzer_config);
  ASSERT_FALSE(config.has_value());
  std::set<std::string> fields;
  for (const ics::config::ConfigError& error : config.error()) {
    fields.insert(error.field);
  }
  EXPECT_EQ(fields, (std::set<std::string>{"strobe.camera_id", "strobe.roi_x_px", "strobe.roi_width_px",
                                           "strobe.pulse_width_ns", "strobe.sweep_steps", "strobe.max_offset_ns",
                                           "strobe.shutter"}));
}

}  // namespace
