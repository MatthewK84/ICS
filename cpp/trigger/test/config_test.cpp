// config_test.cpp: the [trigger] table, as the sample ics-trigger.toml gives it, and its refusals (ICS-040).
#include <chrono>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

#include <ics/common/units.hpp>
#include <ics/common/universal_types.h>
#include <ics/config/config.hpp>
#include <ics/config/reader.hpp>
#include <ics/trigger/config.h>
#include <ics/trigger/controller.h>
#include <ics/trigger/generator.h>

namespace
{

using ics::trigger::Trigger_config;

constexpr ics::uint16 raw_port = 2101;

// The sample's text, with the line that starts with a key replaced.
std::string sample_with(const std::string& key, const std::string& replacement)
{
  std::ifstream file(ICS_TRIGGER_EXAMPLE);
  std::string text;
  std::string line;
  while (std::getline(file, line))
  {
    text += (line.rfind(key, 0) == 0) ? replacement : line;
    text += "\n";
  }
  return text;
}

// Whether the sample is refused with a line replaced.
bool refused_with(const std::string& key, const std::string& replacement)
{
  const auto parsed = ics::config::parse(sample_with(key, replacement));
  EXPECT_TRUE(parsed.has_value());
  return !ics::config::read(*parsed, &ics::trigger::read_trigger_config).has_value();
}

}  // namespace

// JSF deviation D-5: GoogleTest's TEST and EXPECT macros fix the form of the
// test names and expand to code of GoogleTest's own (AV 50, 51 and 209).
TEST(TriggerConfig, ReadsTheSample)
{
  const auto read = ics::config::read_file(ICS_TRIGGER_EXAMPLE, &ics::trigger::read_trigger_config);
  ASSERT_TRUE(read.has_value()) << ics::config::format_errors("sample", read.error());
  const Trigger_config& config = *read;
  EXPECT_EQ(config.link.address, "192.168.1.57");
  EXPECT_EQ(config.link.port, raw_port);
  EXPECT_EQ(config.link.timeout, std::chrono::milliseconds(100));
  EXPECT_EQ(config.generator.count, 2);
  EXPECT_EQ(config.generator.channels[0], 1);
  EXPECT_EQ(config.generator.channels[1], 2);
  EXPECT_EQ(config.generator.width, std::chrono::microseconds(100));
  EXPECT_DOUBLE_EQ(config.generator.level_v, 2.5);
  EXPECT_TRUE(ics::trigger::valid(config.generator));
  const ics::trigger::Trigger_settings& trigger = config.trigger;
  EXPECT_DOUBLE_EQ(trigger.cue.range_origin.height_m, 700.0);
  EXPECT_EQ(trigger.channel_count, 2);
  EXPECT_EQ(trigger.channels[0], "A");
  EXPECT_EQ(trigger.channels[1], "B");
  EXPECT_EQ(trigger.shot.period, std::chrono::seconds(1));
  EXPECT_EQ(trigger.shot.offset, ics::Duration::zero());
  EXPECT_EQ(trigger.shot.command_margin, std::chrono::milliseconds(200));
  EXPECT_EQ(trigger.shot.guard, std::chrono::milliseconds(50));
  EXPECT_EQ(trigger.lead, ics::Duration::zero());
  EXPECT_EQ(trigger.recommit, std::chrono::microseconds(200));
  EXPECT_EQ(trigger.predict_period, std::chrono::milliseconds(100));
  EXPECT_EQ(trigger.arm_timeout, std::chrono::seconds(120));
  EXPECT_EQ(trigger.max_clock_bound, std::chrono::microseconds(100));
  EXPECT_DOUBLE_EQ(trigger.approach.launch_speed_m_s, 3.0);
  EXPECT_EQ(trigger.approach.horizon, std::chrono::seconds(60));
  EXPECT_TRUE(ics::trigger::valid(trigger).has_value());
}

TEST(TriggerConfig, AChannelThatIsNotOneOfThe577sReadsAsNone)
{
  const auto parsed = ics::config::parse(sample_with("channels", "channels = [\"A\", \"J\"]"));
  ASSERT_TRUE(parsed.has_value());
  const auto read = ics::config::read(*parsed, &ics::trigger::read_trigger_config);
  ASSERT_TRUE(read.has_value());
  EXPECT_EQ(read->generator.channels[1], 0);
  EXPECT_FALSE(ics::trigger::valid(read->generator));
}

TEST(TriggerConfig, RefusesBadSettings)
{
  EXPECT_TRUE(refused_with("port", "port = 0"));
  EXPECT_TRUE(refused_with("reply_timeout_ns", "reply_timeout_ns = 0"));
  EXPECT_TRUE(refused_with("channels", "channels = []"));
  EXPECT_TRUE(refused_with("width_ns", "width_ns = 1"));
  EXPECT_TRUE(refused_with("trigger_level", "trigger_level = 20.0"));
  EXPECT_TRUE(refused_with("arm_timeout_ns", "arm_timeout_ns = 0"));
  EXPECT_TRUE(refused_with("launch_speed", "launch_speed = 0.0"));
  EXPECT_TRUE(refused_with("horizon_ns", "horizon_ns = 0"));
  EXPECT_TRUE(refused_with("address", ""));
  EXPECT_TRUE(refused_with("[cue]", "[other]"));
}
