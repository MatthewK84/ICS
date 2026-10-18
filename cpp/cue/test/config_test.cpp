// config_test.cpp: the [cue] table, as the sample ics-cue.toml gives it, and its refusals (ICS-037).
#include <chrono>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

#include <ics/config/config.hpp>
#include <ics/config/reader.hpp>
#include <ics/cue/config.h>
#include <ics/cue/extrapolator.h>
#include <ics/v1/common.pb.h>

namespace
{

// The sample's text, with the line that starts with a key replaced.
std::string sample_with(const std::string& key, const std::string& replacement)
{
  std::ifstream file(ICS_CUE_EXAMPLE);
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
TEST(CueConfig, ReadsTheSample)
{
  const auto read = ics::config::read_file(ICS_CUE_EXAMPLE, &ics::cue::read_replay_config);
  ASSERT_TRUE(read.has_value()) << ics::config::format_errors("sample", read.error());
  const ics::cue::Cue_settings& settings = *read;
  EXPECT_DOUBLE_EQ(settings.range_origin.latitude_deg, 40.0);
  EXPECT_DOUBLE_EQ(settings.station.height_m, 702.0);
  EXPECT_EQ(settings.target_role, ics::v1::ENTITY_ROLE_TARGET);
  EXPECT_EQ(settings.command_latency, std::chrono::milliseconds(50));
  EXPECT_EQ(settings.filter.history, std::chrono::seconds(2));
  EXPECT_EQ(settings.filter.capacity, 64);
  EXPECT_DOUBLE_EQ(settings.filter.gate, 16.27);
  EXPECT_DOUBLE_EQ(settings.defaults.rtk_fixed.vertical_m, 0.05);
  EXPECT_DOUBLE_EQ(settings.defaults.velocity_sigma_m_s, 0.2);
  EXPECT_TRUE(ics::cue::Cue_extrapolator::make(settings).has_value());
}

TEST(CueConfig, RefusesBadSettings)
{
  const auto parsed = ics::config::parse(sample_with("capacity", "capacity = 65"));
  ASSERT_TRUE(parsed.has_value());
  EXPECT_FALSE(ics::config::read(*parsed, &ics::cue::read_replay_config).has_value());
  const auto role = ics::config::parse(sample_with("target_role", "target_role = \"friend\""));
  ASSERT_TRUE(role.has_value());
  EXPECT_FALSE(ics::config::read(*role, &ics::cue::read_replay_config).has_value());
  const auto missing = ics::config::parse(sample_with("gate", ""));
  ASSERT_TRUE(missing.has_value());
  EXPECT_FALSE(ics::config::read(*missing, &ics::cue::read_replay_config).has_value());
}
