// monitor_test.cpp: ics-tracker-monitor against Aravis's fake camera (ICS-038).
#include <array>
#include <ostream>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

#include <ics/common/universal_types.h>
#include <ics/config/reader.hpp>
#include <ics/tracker/centroid.h>
#include <ics/tracker/config.h>
#include <ics/tracker/monitor.h>

namespace
{

using ics::int32;

constexpr ics::int64 frames = 3;
constexpr ics::int64 tiny_buffer = 16;
constexpr ics::int32 whole_frame_area = 65535;

// Counts the lines of a text that hold a key.
int32 lines_with(const std::string& text, const std::string& key)
{
  std::istringstream stream(text);
  std::string line;
  int32 count = 0;
  while (std::getline(stream, line))
  {
    count += (line.find(key) != std::string::npos) ? 1 : 0;
  }
  return count;
}

ics::tracker::Tracker_config sample()
{
  const auto read = ics::config::read_file(ICS_TRACKER_EXAMPLE, &ics::tracker::read_tracker_config);
  EXPECT_TRUE(read.has_value());
  return *read;
}

int32 run(const std::array<const char*, 2>& arguments,
          std::ostream& out,
          std::ostream& errors)
{
  return ics::tracker::run_monitor(arguments, ICS_TRACKER_EXAMPLE, out, errors);
}

}  // namespace

// JSF deviation D-5: GoogleTest's TEST and EXPECT macros fix the form of the
// test names and expand to code of GoogleTest's own (AV 50, 51 and 209).
TEST(TrackerMonitor, MonitorsTheFakeCamerasFrames)
{
  std::ostringstream out;
  std::ostringstream errors;
  EXPECT_EQ(run({"ics-tracker-monitor", "3"}, out, errors), ics::tracker::exit_monitored) << errors.str();
  EXPECT_EQ(lines_with(out.str(), "\"frame\""), frames);
  EXPECT_EQ(lines_with(out.str(), "\"dropped\":0"), 1);
}

TEST(TrackerMonitor, ReportsWhereItFindsTheTarget)
{
  ics::tracker::Tracker_config loose = sample();
  loose.tracker.centroid.k_sigma = 0.0;
  loose.tracker.centroid.min_contrast = 1.0;
  loose.tracker.centroid.max_area = whole_frame_area;
  std::ostringstream out;
  std::ostringstream errors;
  EXPECT_EQ(ics::tracker::monitor_camera(loose, frames, out, errors), ics::tracker::exit_monitored);
  EXPECT_EQ(lines_with(out.str(), "\"xi_urad\""), frames);
}

TEST(TrackerMonitor, WritesFramesItCouldNotTake)
{
  ics::tracker::Tracker_config small = sample();
  small.camera.buffer_bytes = tiny_buffer;
  std::ostringstream out;
  std::ostringstream errors;
  EXPECT_EQ(ics::tracker::monitor_camera(small, frames, out, errors), ics::tracker::exit_monitored);
  EXPECT_EQ(lines_with(out.str(), "\"error\""), frames);
}

TEST(TrackerMonitor, WritesFramesItCannotSearch)
{
  ics::tracker::Tracker_config wide = sample();
  wide.tracker.acquire_window = {ics::tracker::max_window_side, ics::tracker::max_window_side};
  std::ostringstream out;
  std::ostringstream errors;
  EXPECT_EQ(ics::tracker::monitor_camera(wide, frames, out, errors), ics::tracker::exit_monitored);
  EXPECT_EQ(lines_with(out.str(), "\"found\":false"), frames);
}

TEST(TrackerMonitor, FailsWithoutAUsableCamera)
{
  std::ostringstream out;
  std::ostringstream errors;
  ics::tracker::Tracker_config config = sample();
  config.camera.buffers = 1;
  EXPECT_EQ(ics::tracker::monitor_camera(config, frames, out, errors), ics::tracker::exit_failed);
  config = sample();
  config.tracker.gate_px = 0.0;
  EXPECT_EQ(ics::tracker::monitor_camera(config, frames, out, errors), ics::tracker::exit_failed);
  config = sample();
  config.camera_id = "No-Such-Camera";
  EXPECT_EQ(ics::tracker::monitor_camera(config, frames, out, errors), ics::tracker::exit_failed);
  config = sample();
  config.camera.features = "NoSuchFeature=1";
  EXPECT_EQ(ics::tracker::monitor_camera(config, frames, out, errors), ics::tracker::exit_failed);
  EXPECT_EQ(lines_with(errors.str(), "did not"), 2);
}

TEST(TrackerMonitor, TellsItsUsage)
{
  std::ostringstream out;
  std::ostringstream errors;
  EXPECT_EQ(run({"ics-tracker-monitor", "--help"}, out, errors), ics::tracker::exit_monitored);
  EXPECT_EQ(lines_with(out.str(), "usage"), 1);
  EXPECT_EQ(run({"ics-tracker-monitor", "none"}, out, errors), ics::tracker::exit_usage);
  EXPECT_EQ(run({"ics-tracker-monitor", "0"}, out, errors), ics::tracker::exit_usage);
  EXPECT_EQ(run({"ics-tracker-monitor", "3x"}, out, errors), ics::tracker::exit_usage);
  EXPECT_EQ(run({"ics-tracker-monitor", "1000000001"}, out, errors), ics::tracker::exit_usage);
  const std::array<const char*, 1> bare{"ics-tracker-monitor"};
  EXPECT_EQ(ics::tracker::run_monitor(bare, ICS_TRACKER_EXAMPLE, out, errors), ics::tracker::exit_usage);
  const std::array<const char*, 2> three{"ics-tracker-monitor", "3"};
  EXPECT_EQ(ics::tracker::run_monitor(three, "/no/such/file.toml", out, errors),
            ics::tracker::exit_failed);
}
