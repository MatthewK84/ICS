// shot_test.cpp: the edge a pulse is timed from, its delay, and its arming window (ICS-040).
#include <chrono>

#include <gtest/gtest.h>

#include <ics/common/units.hpp>
#include <ics/trigger/shot.h>

namespace
{

using ics::trigger::edge_at_or_before;
using ics::trigger::Shot;
using ics::trigger::shot_for;
using ics::trigger::Shot_settings;
using ics::trigger::window_open;

constexpr ics::Duration period = std::chrono::seconds(1);
constexpr ics::Duration offset = std::chrono::nanoseconds(1500);
constexpr ics::Duration margin = std::chrono::milliseconds(200);
constexpr ics::Duration guard = std::chrono::milliseconds(50);
constexpr ics::Duration tick = std::chrono::nanoseconds(1);
constexpr ics::Duration delay = std::chrono::nanoseconds(123456789);
constexpr ics::Duration half = std::chrono::milliseconds(500);
constexpr ics::Duration late_by = std::chrono::milliseconds(300);
constexpr ics::Duration after_edge = std::chrono::milliseconds(250);
constexpr ics::Duration quarter_period = std::chrono::milliseconds(250);
constexpr Shot_settings settings{period, offset, margin, guard};

const ics::UtcTime edge = ics::UtcTime{} + std::chrono::seconds(100) + offset;

void expect_shot(const Shot& shot,
                 const ics::UtcTime at,
                 const ics::Duration after,
                 const bool late)
{
  EXPECT_EQ(shot.edge, at);
  EXPECT_EQ(shot.delay, after);
  EXPECT_EQ(shot.late, late);
}

}  // namespace

// JSF deviation D-5: GoogleTest's TEST and EXPECT macros fix the form of the
// test names and expand to code of GoogleTest's own (AV 50, 51 and 209).
TEST(Shot, AnEdgeIsAWholeSecondPlusTheOffset)
{
  EXPECT_EQ(edge_at_or_before(edge, settings), edge);
  EXPECT_EQ(edge_at_or_before(edge - tick, settings), edge - period);
  EXPECT_EQ(edge_at_or_before((edge + period) - tick, settings), edge);
  EXPECT_EQ(edge_at_or_before(ics::UtcTime{}, settings), (ics::UtcTime{} + offset) - period);
}

TEST(Shot, APulseIsTimedFromTheLastEdgeBeforeIt)
{
  const Shot shot = shot_for(edge + delay, edge - half, settings);
  expect_shot(shot, edge, delay, false);
  EXPECT_EQ(ics::trigger::fire_time(shot), edge + delay);
  expect_shot(shot_for(edge, edge - margin, settings), edge, ics::Duration::zero(), false);
}

TEST(Shot, APulseTooLateForItsEdgeGoesOutOnTheNextThatCanBeArmedFor)
{
  const ics::UtcTime fire = edge + late_by;
  expect_shot(shot_for(fire, (edge - margin) + tick, settings), edge + period, ics::Duration::zero(), true);
  expect_shot(shot_for(fire, edge + after_edge, settings), edge + period, ics::Duration::zero(), true);
  expect_shot(shot_for(fire, (edge + period) - margin, settings), edge + period, ics::Duration::zero(), true);
  const ics::UtcTime past = ((edge + period) - margin) + tick;
  expect_shot(shot_for(fire, past, settings), edge + period + period, ics::Duration::zero(), true);
}

TEST(Shot, TheGeneratorMayBeArmedOnlyBetweenTheEdgeBeforeAndTheMargin)
{
  const Shot shot{edge, late_by, false};
  const ics::UtcTime opens = (edge - period) + guard;
  EXPECT_FALSE(window_open(shot, opens - tick, settings));
  EXPECT_TRUE(window_open(shot, opens, settings));
  EXPECT_TRUE(window_open(shot, edge - margin, settings));
  EXPECT_FALSE(window_open(shot, (edge - margin) + tick, settings));
}

TEST(Shot, APeriodMustHoldTheMargins)
{
  EXPECT_TRUE(ics::trigger::valid(settings));
  EXPECT_FALSE(ics::trigger::valid(Shot_settings{period, offset, -tick, guard}));
  EXPECT_FALSE(ics::trigger::valid(Shot_settings{period, offset, margin, -tick}));
  EXPECT_FALSE(ics::trigger::valid(Shot_settings{quarter_period, offset, margin, guard}));
}
