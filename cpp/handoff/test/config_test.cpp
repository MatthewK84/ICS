// config_test.cpp: the [handoff] table, as the sample ics-handoff.toml gives it, and its refusals (ICS-039).
#include <chrono>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

#include <ics/config/config.hpp>
#include <ics/config/reader.hpp>
#include <ics/handoff/config.h>
#include <ics/handoff/scheduler.h>

namespace
{

using ics::handoff::Handoff_settings;

// The sample's text, with the line that starts with a key replaced.
std::string sample_with(const std::string& key, const std::string& replacement)
{
  std::ifstream file(ICS_HANDOFF_EXAMPLE);
  std::string text;
  std::string line;
  while (std::getline(file, line))
  {
    text += (line.rfind(key, 0) == 0) ? replacement : line;
    text += "\n";
  }
  return text;
}

}  // namespace

// JSF deviation D-5: GoogleTest's TEST and EXPECT macros fix the form of the
// test names and expand to code of GoogleTest's own (AV 50, 51 and 209).
TEST(HandoffConfig, ReadsTheSample)
{
  const auto read = ics::config::read_file(ICS_HANDOFF_EXAMPLE, &ics::handoff::read_handoff_config);
  ASSERT_TRUE(read.has_value()) << ics::config::format_errors("sample", read.error());
  const Handoff_settings& settings = *read;
  EXPECT_DOUBLE_EQ(settings.cue.station.height_m, 702.0);
  EXPECT_EQ(settings.cue.command_latency, std::chrono::milliseconds(50));
  EXPECT_EQ(settings.count, 2);
  EXPECT_EQ(settings.pairings[0].target, "uas-1");
  EXPECT_EQ(settings.pairings[0].interceptor, "int-1");
  EXPECT_EQ(settings.pairings[1].target, "uas-2");
  EXPECT_EQ(settings.pairings[1].interceptor, "int-2");
  EXPECT_EQ(settings.queue.min_gap, std::chrono::seconds(3));
  EXPECT_EQ(settings.queue.dwell, std::chrono::milliseconds(500));
  EXPECT_EQ(settings.queue.margin, std::chrono::milliseconds(500));
  EXPECT_EQ(settings.queue.settle, std::chrono::milliseconds(300));
  EXPECT_DOUBLE_EQ(settings.queue.engaged_miss_m, 10.0);
  EXPECT_DOUBLE_EQ(settings.approach.launch_speed_m_s, 3.0);
  EXPECT_EQ(settings.approach.horizon, std::chrono::seconds(60));
  EXPECT_DOUBLE_EQ(settings.slew.rate_rad_s, 0.17);
  EXPECT_DOUBLE_EQ(settings.slew.acceleration_rad_s2, 0.5);
  EXPECT_DOUBLE_EQ(settings.slew.gain, 6.0);
  EXPECT_TRUE(ics::handoff::valid(settings).has_value());
}

TEST(HandoffConfig, ListsOfTwoLengthsLeaveAVehicleUnnamed)
{
  const auto fewer = ics::config::parse(sample_with("interceptors", "interceptors = [\"int-1\"]"));
  ASSERT_TRUE(fewer.has_value());
  const auto read = ics::config::read(*fewer, &ics::handoff::read_handoff_config);
  ASSERT_TRUE(read.has_value());
  EXPECT_EQ(read->count, 2);
  EXPECT_TRUE(read->pairings[1].interceptor.empty());
  EXPECT_FALSE(ics::handoff::valid(*read).has_value());
  const auto more = ics::config::parse(sample_with("targets", "targets = [\"uas-1\"]"));
  ASSERT_TRUE(more.has_value());
  const auto unnamed = ics::config::read(*more, &ics::handoff::read_handoff_config);
  ASSERT_TRUE(unnamed.has_value());
  EXPECT_TRUE(unnamed->pairings[1].target.empty());
  EXPECT_FALSE(ics::handoff::valid(*unnamed).has_value());
}

TEST(HandoffConfig, RefusesBadSettings)
{
  const auto slow = ics::config::parse(sample_with("slew_rate", "slew_rate = 0.0"));
  ASSERT_TRUE(slow.has_value());
  EXPECT_FALSE(ics::config::read(*slow, &ics::handoff::read_handoff_config).has_value());
  const auto crowded = ics::config::parse(
    sample_with("targets", "targets = [\"1\", \"2\", \"3\", \"4\", \"5\", \"6\", \"7\", \"8\", \"9\"]"));
  ASSERT_TRUE(crowded.has_value());
  EXPECT_FALSE(ics::config::read(*crowded, &ics::handoff::read_handoff_config).has_value());
  const auto missing = ics::config::parse(sample_with("dwell_ns", ""));
  ASSERT_TRUE(missing.has_value());
  EXPECT_FALSE(ics::config::read(*missing, &ics::handoff::read_handoff_config).has_value());
}
