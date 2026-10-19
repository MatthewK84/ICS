// config_test.cpp: the [tracker] table and its refusals (ICS-038).
#include <chrono>
#include <fstream>
#include <numbers>
#include <string>

#include <gtest/gtest.h>

#include <ics/common/universal_types.h>
#include <ics/config/config.hpp>
#include <ics/config/reader.hpp>
#include <ics/tracker/aravis_camera.h>
#include <ics/tracker/camera.h>
#include <ics/tracker/centroid.h>
#include <ics/tracker/config.h>
#include <ics/tracker/tracker.h>

namespace
{

constexpr ics::int32 region_width = 128;
constexpr ics::int32 buffers = 8;
constexpr ics::float64 plate_scale_rad = 2.0E-5;
constexpr ics::float64 boresight_x = 64.0;
constexpr ics::int32 acquire_side = 64;
constexpr ics::int32 confirm_frames = 5;
constexpr ics::float64 roll_rad = std::numbers::pi / 2.0;
constexpr ics::float64 tight = 1.0E-15;

// The sample's text, with the line that starts with a key replaced.
std::string sample_with(const std::string& key, const std::string& replacement)
{
  std::ifstream file(ICS_TRACKER_EXAMPLE);
  std::string text;
  std::string line;
  while (std::getline(file, line))
  {
    text += (line.rfind(key, 0) == 0) ? replacement : line;
    text += "\n";
  }
  return text;
}

// Whether a sample with one line replaced is refused.
bool refused(const std::string& key, const std::string& replacement)
{
  const auto parsed = ics::config::parse(sample_with(key, replacement));
  return parsed.has_value() && !ics::config::read(*parsed, &ics::tracker::read_tracker_config).has_value();
}

}  // namespace

// JSF deviation D-5: GoogleTest's TEST and EXPECT macros fix the form of the
// test names and expand to code of GoogleTest's own (AV 50, 51 and 209).
TEST(TrackerConfig, ReadsTheSample)
{
  const auto read = ics::config::read_file(ICS_TRACKER_EXAMPLE, &ics::tracker::read_tracker_config);
  ASSERT_TRUE(read.has_value()) << ics::config::format_errors("sample", read.error());
  const ics::tracker::Tracker_config& config = *read;
  EXPECT_EQ(config.camera_id, "Fake_1");
  EXPECT_TRUE(config.fake_camera);
  EXPECT_EQ(config.camera.format, ics::tracker::Pixel_format::mono16);
  EXPECT_EQ(config.camera.region.width, region_width);
  EXPECT_EQ(config.camera.buffers, buffers);
  EXPECT_EQ(config.camera.buffer_bytes, 0);
  EXPECT_EQ(config.camera.stamps, ics::tracker::Stamp_source::camera);
  EXPECT_DOUBLE_EQ(config.tracker.camera.boresight_x, boresight_x);
  EXPECT_DOUBLE_EQ(config.tracker.camera.plate_scale_rad, plate_scale_rad);
  EXPECT_FALSE(config.tracker.camera.mirrored);
  EXPECT_EQ(config.tracker.centroid.polarity, ics::tracker::Polarity::either);
  EXPECT_EQ(config.tracker.acquire_window.width, acquire_side);
  EXPECT_EQ(config.tracker.confirm_frames, confirm_frames);
  EXPECT_EQ(config.tracker.command_latency, std::chrono::milliseconds(50));
  EXPECT_TRUE(ics::tracker::valid(config.camera).has_value());
  EXPECT_TRUE(ics::tracker::valid(config.tracker).has_value());
}

TEST(TrackerConfig, TurnsTheRollIntoRadians)
{
  const auto parsed = ics::config::parse(sample_with("roll_deg", "roll_deg = 90.0"));
  ASSERT_TRUE(parsed.has_value());
  const auto read = ics::config::read(*parsed, &ics::tracker::read_tracker_config);
  ASSERT_TRUE(read.has_value());
  EXPECT_NEAR(read->tracker.camera.roll_rad, roll_rad, tight);
}

TEST(TrackerConfig, JoinsTheFeatures)
{
  const std::string features = "features = [\"PtpEnable=true\", \"GevSCPSPacketSize=1500\"]";
  const auto parsed = ics::config::parse(sample_with("features", features));
  ASSERT_TRUE(parsed.has_value());
  const auto read = ics::config::read(*parsed, &ics::tracker::read_tracker_config);
  ASSERT_TRUE(read.has_value());
  EXPECT_EQ(read->camera.features, "PtpEnable=true GevSCPSPacketSize=1500");
}

TEST(TrackerConfig, RefusesBadSettings)
{
  EXPECT_TRUE(refused("pixel_format", "pixel_format = \"rgb8\""));
  EXPECT_TRUE(refused("time_stamps", "time_stamps = \"gps\""));
  EXPECT_TRUE(refused("polarity", "polarity = \"grey\""));
  EXPECT_TRUE(refused("buffers", "buffers = 1"));
  EXPECT_TRUE(refused("acquire_window_px", "acquire_window_px = 300"));
  EXPECT_TRUE(refused("plate_scale_urad", "plate_scale_urad = 0.0"));
  EXPECT_TRUE(refused("gain", ""));
}
