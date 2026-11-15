// queue_test.cpp: the queue's order, the engagement served, and its flags (ICS-039).
#include <chrono>
#include <cstddef>
#include <initializer_list>
#include <numbers>
#include <optional>

#include <gtest/gtest.h>

#include <ics/common/units.hpp>
#include <ics/common/universal_types.h>
#include <ics/encoder/pointing_model.hpp>
#include <ics/handoff/approach.h>
#include <ics/handoff/queue.h>
#include <ics/handoff/slew.h>

namespace
{

using ics::float64;
using ics::int32;
using ics::int64;
using ics::encoder::Direction;
using ics::handoff::Approach;
using ics::handoff::Plan;
using ics::handoff::Queue;
using ics::handoff::Queue_settings;
using ics::handoff::Sighting;
using ics::handoff::Slot;

constexpr Queue_settings settings{std::chrono::seconds(3),
                                  std::chrono::milliseconds(500),
                                  std::chrono::milliseconds(500),
                                  std::chrono::milliseconds(300),
                                  10.0};
constexpr ics::handoff::Slew_limits limits{0.17, 0.5, 6.0};
constexpr float64 near_miss_m = 1.0;
constexpr float64 widest_miss_m = 10.0;
constexpr float64 fly_by_m = 10.1;
constexpr float64 sixty_degrees = std::numbers::pi / 3.0;
constexpr Direction near{1.0, 0.3};
constexpr Direction far{1.0 + sixty_degrees, 0.3};
// Times in milliseconds from the start. The first engagement's approach,
// and moments around it: the update before, during the dwell, and after it.
constexpr int64 first = 8000;
constexpr int64 before_first = 7900;
constexpr int64 dwelling = 8400;
constexpr int64 moved = 8500;
constexpr int64 after_moved = 8600;
// Approaches inside and past the margin of the first, either side.
constexpr int64 inside_margin = 7500;
constexpr int64 past_margin = 7400;
constexpr int64 later = 9000;
// A second approach too soon after the first, the update before it, one clear of it, and one long after.
constexpr int64 soon = 10000;
constexpr int64 before_soon = 9900;
constexpr int64 clear = 11100;
constexpr int64 soon_after_second = 13900;
constexpr int64 long_after = 20000;
constexpr int64 before_long_after = 19900;
constexpr int64 idle = 20500;
constexpr int64 much_later = 30000;
constexpr int64 never = 40000;
// A third approach the mount can and cannot reach from the first, 60 degrees away.
constexpr int64 out_of_reach = 14000;
constexpr int64 in_reach = 16000;
constexpr int64 tick = 100;
constexpr int64 two_ticks = 200;
constexpr int64 again = 12000;
constexpr int32 three = 3;
constexpr int32 four = 4;

const ics::UtcTime start = ics::UtcTime{} + std::chrono::seconds(100);

ics::UtcTime at(const int64 milliseconds)
{
  return start + std::chrono::milliseconds(milliseconds);
}

// A sighting of an approach, with the target's direction when given one.
Sighting seen(const int64 milliseconds,
              const float64 miss_m,
              const std::optional<Direction>& direction)
{
  return Sighting{Approach{at(milliseconds), miss_m}, direction};
}

// A sighting of an approach that misses by a metre, with the target near.
Sighting seen(const int64 milliseconds)
{
  return seen(milliseconds, near_miss_m, near);
}

// The plan after an update at a time with these sightings, in the plan's order.
Plan update(Queue& queue,
            const int64 milliseconds,
            const std::initializer_list<Sighting> given)
{
  ics::handoff::Sightings sightings{};
  int32 place = 0;
  for (const Sighting& sighting : given)
  {
    sightings[static_cast<std::size_t>(place)] = sighting;
    ++place;
  }
  return queue.update(at(milliseconds), sightings);
}

const Slot& slot(const Plan& plan, const int32 engagement)
{
  return plan.slots[static_cast<std::size_t>(engagement)];
}

const Sighting none{};

}  // namespace

// JSF deviation D-5: GoogleTest's TEST and EXPECT macros fix the form of the
// test names and expand to code of GoogleTest's own (AV 50, 51 and 209).
TEST(Queue, EngagementsWithoutAnApproachWaitInThePlansOrder)
{
  Queue queue(three, settings, limits);
  const Plan plan = update(queue, 0, {none, none, none});
  EXPECT_EQ(plan.count, three);
  EXPECT_EQ(plan.waiting, three);
  EXPECT_EQ(plan.order[0], 0);
  EXPECT_EQ(plan.order[1], 1);
  EXPECT_EQ(plan.order[2], 2);
  EXPECT_EQ(plan.current, 0);
  EXPECT_FALSE(plan.dwelling);
}

TEST(Queue, EngagementsWithAnApproachComeFirstSoonestFirst)
{
  Queue queue(four, settings, limits);
  const Plan plan = update(queue, 0, {none, seen(later), seen(first), seen(later)});
  EXPECT_EQ(plan.order[0], 2);
  EXPECT_EQ(plan.order[1], 1);
  EXPECT_EQ(plan.order[2], three);
  EXPECT_EQ(plan.order[static_cast<std::size_t>(three)], 0);
  EXPECT_EQ(plan.current, 2);
}

TEST(Queue, TheStationLeavesAnEngagementOnlyForOneMoreThanTheMarginSooner)
{
  Queue queue(2, settings, limits);
  EXPECT_EQ(update(queue, 0, {seen(first), seen(later)}).current, 0);
  const Plan inside = update(queue, tick, {seen(first), seen(inside_margin)});
  EXPECT_EQ(inside.order[0], 1);
  EXPECT_EQ(inside.current, 0);
  EXPECT_EQ(update(queue, two_ticks, {seen(first), seen(past_margin)}).current, 1);
}

TEST(Queue, TheStationLeavesAnEngagementWithNoApproachForOneWith)
{
  Queue queue(2, settings, limits);
  EXPECT_EQ(update(queue, 0, {none, none}).current, 0);
  EXPECT_EQ(update(queue, tick, {none, seen(long_after)}).current, 1);
  EXPECT_EQ(update(queue, two_ticks, {none, none}).current, 1);
}

TEST(Queue, AnEngagementIsDoneWhenItsApproachPassesNearEnough)
{
  Queue queue(2, settings, limits);
  static_cast<void>(update(queue, before_first, {seen(first, widest_miss_m, near), seen(long_after)}));
  const Plan plan = update(queue, first, {none, seen(long_after)});
  EXPECT_TRUE(slot(plan, 0).done);
  EXPECT_TRUE(slot(plan, 0).timed);
  EXPECT_EQ(slot(plan, 0).approach.time, at(first));
  EXPECT_DOUBLE_EQ(slot(plan, 0).approach.miss_m, widest_miss_m);
  EXPECT_EQ(plan.waiting, 1);
  EXPECT_EQ(plan.order[0], 1);
  EXPECT_EQ(plan.current, 0);
  EXPECT_TRUE(plan.dwelling);
}

TEST(Queue, TheStationDwellsOnADoneEngagementThenMovesOnAndEndsIdle)
{
  Queue queue(2, settings, limits);
  static_cast<void>(update(queue, before_first, {seen(first), seen(long_after)}));
  EXPECT_TRUE(update(queue, dwelling, {none, seen(long_after)}).dwelling);
  const Plan next = update(queue, moved, {none, seen(long_after)});
  EXPECT_EQ(next.current, 1);
  EXPECT_FALSE(next.dwelling);
  static_cast<void>(update(queue, before_long_after, {none, seen(long_after)}));
  EXPECT_TRUE(update(queue, long_after, {none, none}).dwelling);
  const Plan ended = update(queue, idle, {none, none});
  EXPECT_EQ(ended.waiting, 0);
  EXPECT_EQ(ended.current, std::nullopt);
  EXPECT_FALSE(ended.dwelling);
  EXPECT_EQ(update(queue, much_later, {seen(never), seen(never)}).current, std::nullopt);
}

TEST(Queue, AFlyByIsNotAnEngagement)
{
  Queue queue(1, settings, limits);
  static_cast<void>(update(queue, before_first, {seen(first, fly_by_m, near)}));
  const Plan passed = update(queue, first, {none});
  EXPECT_FALSE(slot(passed, 0).done);
  EXPECT_FALSE(slot(passed, 0).timed);
  EXPECT_TRUE(slot(update(queue, later, {seen(again)}), 0).timed);
  EXPECT_TRUE(slot(update(queue, again, {none}), 0).done);
}

TEST(Queue, AnApproachLostAsItPassesIsHeld)
{
  Queue queue(1, settings, limits);
  static_cast<void>(update(queue, past_margin, {seen(first)}));
  const Plan held = update(queue, inside_margin, {none});
  EXPECT_TRUE(slot(held, 0).timed);
  EXPECT_EQ(slot(held, 0).approach.time, at(first));
  // An approach at the time of the update is not ahead of it.
  EXPECT_FALSE(slot(update(queue, before_first, {seen(before_first)}), 0).done);
  EXPECT_TRUE(slot(update(queue, first, {none}), 0).done);
}

TEST(Queue, AnApproachGivenUpEarlyIsDropped)
{
  Queue queue(1, settings, limits);
  static_cast<void>(update(queue, inside_margin - tick, {seen(first)}));
  EXPECT_FALSE(slot(update(queue, past_margin, {none}), 0).timed);
  EXPECT_FALSE(slot(update(queue, first + tick, {none}), 0).done);
}

TEST(Queue, AnEngagementTooSoonAfterTheOneBeforeIsFlagged)
{
  Queue queue(three, settings, limits);
  const Plan plan = update(queue, 0, {seen(first), seen(soon), seen(soon_after_second)});
  EXPECT_FALSE(slot(plan, 0).too_close);
  EXPECT_TRUE(slot(plan, 1).too_close);
  EXPECT_FALSE(slot(plan, 2).too_close);
  EXPECT_FALSE(slot(plan, 1).out_of_reach);
}

TEST(Queue, AnEngagementTheMountCannotReachInTheGapIsFlagged)
{
  Queue queue(three, settings, limits);
  const Sighting unseen = seen(much_later, near_miss_m, std::nullopt);
  const Plan plan = update(queue, 0, {seen(first), seen(out_of_reach, near_miss_m, far), unseen});
  EXPECT_FALSE(slot(plan, 0).out_of_reach);
  EXPECT_TRUE(slot(plan, 1).out_of_reach);
  EXPECT_FALSE(slot(plan, 2).out_of_reach);
  EXPECT_FALSE(slot(update(queue, tick, {seen(first), seen(in_reach, near_miss_m, far), unseen}), 1).out_of_reach);
  const Sighting blind = seen(first, near_miss_m, std::nullopt);
  EXPECT_FALSE(slot(update(queue, two_ticks, {blind, seen(out_of_reach, near_miss_m, far), none}), 1).out_of_reach);
}

TEST(Queue, TheFirstEngagementIsFlaggedAgainstTheOneServedBeforeIt)
{
  Queue queue(2, settings, limits);
  static_cast<void>(update(queue, before_first, {seen(first), seen(soon)}));
  const Plan staying = update(queue, first, {none, seen(soon)});
  EXPECT_TRUE(staying.dwelling);
  EXPECT_TRUE(slot(staying, 1).too_close);
  const Plan next = update(queue, moved, {none, seen(soon)});
  EXPECT_EQ(next.current, 1);
  EXPECT_TRUE(slot(next, 1).too_close);
  EXPECT_FALSE(slot(update(queue, after_moved, {none, seen(clear)}), 1).too_close);
  const Plan done = update(queue, clear, {none, none});
  EXPECT_TRUE(slot(done, 1).done);
  EXPECT_FALSE(slot(done, 1).too_close);
}

TEST(Queue, FlagsFallWhenAnEngagementComesFirstWithNoneServed)
{
  Queue queue(2, settings, limits);
  EXPECT_TRUE(slot(update(queue, 0, {seen(first), seen(later)}), 1).too_close);
  const Plan plan = update(queue, tick, {none, seen(later)});
  EXPECT_EQ(plan.order[0], 1);
  EXPECT_FALSE(slot(plan, 1).too_close);
  EXPECT_FALSE(slot(plan, 0).too_close);
}

TEST(Queue, ADoneEngagementKeepsItsFlags)
{
  Queue queue(2, settings, limits);
  static_cast<void>(update(queue, before_first, {seen(first), seen(soon)}));
  static_cast<void>(update(queue, moved, {none, seen(soon)}));
  static_cast<void>(update(queue, before_soon, {none, seen(soon)}));
  EXPECT_TRUE(slot(update(queue, soon, {none, none}), 1).too_close);
  EXPECT_TRUE(slot(update(queue, again, {none, none}), 1).too_close);
}

TEST(Queue, ARunPlanHasOneToEightEngagements)
{
  Queue empty(0, settings, limits);
  EXPECT_EQ(update(empty, 0, {none}).count, 1);
  Queue crowded(ics::handoff::max_engagements + 1, settings, limits);
  EXPECT_EQ(update(crowded, 0, {none}).count, 1);
  Queue full(ics::handoff::max_engagements, settings, limits);
  EXPECT_EQ(update(full, 0, {none}).waiting, ics::handoff::max_engagements);
}

TEST(Queue, AQueueMadeWithSettingsItCannotUseFailsACheck)
{
  Queue timeless(1, Queue_settings{{}, {}, {}, {}, 0.0}, limits);
  EXPECT_EQ(update(timeless, 0, {none}).count, 1);
  Queue still(1, settings, ics::handoff::Slew_limits{0.0, 0.0, 0.0});
  EXPECT_EQ(update(still, 0, {none}).count, 1);
}

TEST(Queue, SettingsMustHaveTimesThatAreNotNegativeAndAMissDistance)
{
  const ics::Duration negative = std::chrono::nanoseconds(-1);
  EXPECT_TRUE(ics::handoff::valid(settings));
  EXPECT_FALSE(ics::handoff::valid(Queue_settings{negative, {}, {}, {}, near_miss_m}));
  EXPECT_FALSE(ics::handoff::valid(Queue_settings{{}, negative, {}, {}, near_miss_m}));
  EXPECT_FALSE(ics::handoff::valid(Queue_settings{{}, {}, negative, {}, near_miss_m}));
  EXPECT_FALSE(ics::handoff::valid(Queue_settings{{}, {}, {}, negative, near_miss_m}));
  EXPECT_FALSE(ics::handoff::valid(Queue_settings{{}, {}, {}, {}, 0.0}));
}
