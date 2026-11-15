// generator_test.cpp: the 577's driver against an emulated 577 (ICS-040).
#include <chrono>
#include <cstddef>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <emulated_577.h>
#include <ics/common/error.hpp>
#include <ics/common/units.hpp>
#include <ics/common/universal_types.h>
#include <ics/trigger/generator.h>

namespace
{

using ics::int32;
using ics::trigger::Bnc577;
using ics::trigger::Bnc577_settings;
using ics::trigger::channel_number;
using ics::trigger_test::Emulated_577;

constexpr ics::Duration width = std::chrono::microseconds(100);
constexpr ics::Duration delay = std::chrono::nanoseconds(123456789);
constexpr ics::Duration too_narrow = std::chrono::nanoseconds(9);
constexpr ics::Duration too_wide = std::chrono::milliseconds(1001);
constexpr ics::float64 level_v = 2.5;
constexpr ics::float64 low_level_v = 0.25;
constexpr ics::float64 too_low_v = 0.19;
constexpr ics::float64 too_high_v = 15.01;
constexpr int32 third = 3;
constexpr int32 ninth = 9;
// Where the commands of a shot come after the set-up's 16, and where the set-up's level is.
constexpr std::size_t first_delay_at = 16U;
constexpr std::size_t second_delay_at = 17U;
constexpr std::size_t level_at = 5U;
constexpr std::size_t before_refusal = 3U;
// The set-up's commands when no channel is in use.
constexpr std::size_t system_commands = 6U;

const ics::UtcTime edge = ics::UtcTime{} + std::chrono::seconds(100);

// Channels A and C.
Bnc577_settings settings()
{
  return Bnc577_settings{{1, third}, 2, width, level_v};
}

}  // namespace

// JSF deviation D-5: GoogleTest's TEST and EXPECT macros fix the form of the
// test names and expand to code of GoogleTest's own (AV 50, 51 and 209).
TEST(Generator, SetsThe577UpForOnePulseAChannelOnThePps)
{
  Emulated_577 instrument;
  Bnc577 generator(instrument, settings());
  ASSERT_TRUE(generator.set_up().has_value());
  const std::vector<std::string> expected{"*IDN?",
                                          ":PULSE0:STATE OFF",
                                          ":PULSE0:MODE SING",
                                          ":PULSE0:TRIG:MODE TRIG",
                                          ":PULSE0:TRIG:EDGE RIS",
                                          ":PULSE0:TRIG:LEV 2.50",
                                          ":PULSE1:CMODE SING",
                                          ":PULSE1:SYNC T0",
                                          ":PULSE1:POL NORM",
                                          ":PULSE1:WIDT 0.000100000",
                                          ":PULSE1:STATE ON",
                                          ":PULSE3:CMODE SING",
                                          ":PULSE3:SYNC T0",
                                          ":PULSE3:POL NORM",
                                          ":PULSE3:WIDT 0.000100000",
                                          ":PULSE3:STATE ON"};
  EXPECT_EQ(instrument.commands, expected);
  EXPECT_EQ(generator.identity(), ics::trigger_test::identity);
}

TEST(Generator, AShotGivesOnePulseAChannelTheDelayAfterTheFirstEdge)
{
  Emulated_577 instrument;
  Bnc577 generator(instrument, settings());
  ASSERT_TRUE(generator.set_up().has_value());
  ASSERT_TRUE(generator.program(delay).has_value());
  EXPECT_EQ(instrument.commands[first_delay_at], ":PULSE1:DEL 0.123456789");
  EXPECT_EQ(instrument.commands[second_delay_at], ":PULSE3:DEL 0.123456789");
  instrument.edge(edge - std::chrono::seconds(1));
  EXPECT_TRUE(instrument.pulses.empty());
  ASSERT_TRUE(generator.arm().has_value());
  instrument.edge(edge);
  instrument.edge(edge + std::chrono::seconds(1));
  ASSERT_EQ(instrument.pulses.size(), 2U);
  EXPECT_EQ(instrument.pulses[0].channel, 1);
  EXPECT_EQ(instrument.pulses[0].time, edge + delay);
  EXPECT_EQ(instrument.pulses[1].channel, third);
  EXPECT_EQ(instrument.pulses[1].time, edge + delay);
  ASSERT_TRUE(generator.disarm().has_value());
  EXPECT_FALSE(instrument.armed);
  instrument.edge(edge + std::chrono::seconds(2));
  EXPECT_EQ(instrument.pulses.size(), 2U);
}

TEST(Generator, A577ThatDoesNotAnswerIsUnavailable)
{
  Emulated_577 instrument;
  Bnc577 generator(instrument, settings());
  instrument.silent = true;
  EXPECT_EQ(generator.set_up().error(), ics::Error::kUnavailable);
  EXPECT_EQ(generator.arm().error(), ics::Error::kUnavailable);
}

TEST(Generator, AnAnswerThatIsNotAReplyIsMalformed)
{
  Emulated_577 instrument;
  Bnc577 generator(instrument, settings());
  instrument.garbled = true;
  EXPECT_EQ(generator.set_up().error(), ics::Error::kMalformed);
  EXPECT_EQ(generator.disarm().error(), ics::Error::kMalformed);
}

TEST(Generator, ARefusedCommandStopsTheSetUp)
{
  Emulated_577 instrument;
  Bnc577 generator(instrument, settings());
  instrument.refused = ":PULSE0:MODE";
  EXPECT_EQ(generator.set_up().error(), ics::Error::kInvalidArgument);
  EXPECT_EQ(instrument.commands.size(), before_refusal);
  instrument.refused = ":PULSE3:DEL";
  EXPECT_EQ(generator.program(delay).error(), ics::Error::kInvalidArgument);
}

TEST(Generator, ALevelIsSentToTheHundredthOfAVolt)
{
  Emulated_577 instrument;
  Bnc577_settings low = settings();
  low.level_v = low_level_v;
  Bnc577 generator(instrument, low);
  ASSERT_TRUE(generator.set_up().has_value());
  EXPECT_EQ(instrument.commands[level_at], ":PULSE0:TRIG:LEV 0.25");
}

TEST(Generator, ChannelsAreNamedAToH)
{
  EXPECT_EQ(channel_number("A"), 1);
  EXPECT_EQ(channel_number("H"), ics::trigger::max_channels);
  EXPECT_EQ(channel_number("I"), 0);
  EXPECT_EQ(channel_number("@"), 0);
  EXPECT_EQ(channel_number("AB"), 0);
  EXPECT_EQ(channel_number(""), 0);
}

TEST(Generator, SettingsMustBeOnesThe577Takes)
{
  EXPECT_TRUE(ics::trigger::valid(settings()));
  EXPECT_FALSE(ics::trigger::valid(Bnc577_settings{{1}, 0, width, level_v}));
  EXPECT_FALSE(ics::trigger::valid(Bnc577_settings{{1}, ninth, width, level_v}));
  EXPECT_FALSE(ics::trigger::valid(Bnc577_settings{{0}, 1, width, level_v}));
  EXPECT_FALSE(ics::trigger::valid(Bnc577_settings{{ninth}, 1, width, level_v}));
  EXPECT_FALSE(ics::trigger::valid(Bnc577_settings{{1}, 1, too_narrow, level_v}));
  EXPECT_FALSE(ics::trigger::valid(Bnc577_settings{{1}, 1, too_wide, level_v}));
  EXPECT_FALSE(ics::trigger::valid(Bnc577_settings{{1}, 1, width, too_low_v}));
  EXPECT_FALSE(ics::trigger::valid(Bnc577_settings{{1}, 1, width, too_high_v}));
}

TEST(Generator, ADriverMadeWithSettingsThe577DoesNotTakeUsesNoChannel)
{
  Emulated_577 instrument;
  Bnc577 generator(instrument, Bnc577_settings{{ninth}, 1, width, level_v});
  ASSERT_TRUE(generator.set_up().has_value());
  EXPECT_EQ(instrument.commands.size(), system_commands);
}
