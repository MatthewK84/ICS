// replay_test.cpp: ICS-039's "Done when" test: the M-2 replay hands off within 3 s.
// M-2 is milestone M2's replay of two engagements. No record of two
// engagements is in the repository, so they are flown synthetically, as
// world.h describes: two targets 60 to 70 m apart, up to 14.4 degrees apart
// from a station, each hit by its interceptor, the closest approaches 3.0 s
// apart. Three stations 250 to 300 m away each run the scheduler, its tracker
// and a simulated mount of 10 degrees a second and 1 rad/s^2. Each station
// must be tracking the second target no more than 3 s after the first
// engagement's closest approach, with each target in the central half of the
// field at its own closest approach.
#include <chrono>
#include <cmath>
#include <iostream>
#include <numbers>
#include <optional>

#include <gtest/gtest.h>

#include <ics/common/units.hpp>
#include <ics/common/universal_types.h>
#include <ics/cue/extrapolator.h>
#include <ics/encoder/pointing_model.hpp>
#include <ics/handoff/queue.h>
#include <ics/handoff/scheduler.h>
#include <ics/tracker/tracker.h>
#include <world.h>

namespace
{

using ics::float64;
using ics::int32;
using ics::int64;
using ics::handoff::Handoff_status;
using ics::handoff::Slot;
using ics::handoff_test::first_target_place;
using ics::handoff_test::second_target_place;
using ics::handoff_test::Station_world;
using ics::tracker::Mode;

constexpr float64 first_hit_s = 8.0;
constexpr float64 second_hit_s = 11.0;
constexpr float64 close_hit_s = 10.0;
constexpr int64 first_hit_ns = 8000000000;
constexpr int64 second_hit_ns = 11000000000;
constexpr int64 close_hit_ns = 10000000000;
constexpr int64 end_ns = 12500000000;
constexpr float64 budget_s = 3.0;
constexpr float64 off_azimuth = 0.05;
constexpr float64 off_elevation = -0.02;
constexpr float64 time_tolerance_s = 0.05;
constexpr float64 miss_tolerance_m = 2.0;
constexpr float64 never_s = -1.0;
constexpr float64 per_degree = 180.0 / std::numbers::pi;

// What a station did in a replay.
struct Outcome
{
  // When its tracker was first tracking each target, in seconds since the start.
  float64 first_tracked_s = never_s;
  float64 second_tracked_s = never_s;
  // Whether each target was tracked in the central half of the field at its closest approach.
  bool first_centred = false;
  bool second_centred = false;
  // How far apart the targets were from the station when it left the first, in radians.
  float64 apart_rad = 0.0;
  // Whether the tracker still held the second target at the end, with no
  // engagement left, and whether the scheduler still had a cue to give.
  bool tracking_at_end = false;
  bool cued_at_end = true;
  Handoff_status last{};
};

// The angle between two vehicles from a station now.
float64 apart(const Station_world& world)
{
  const std::optional<ics::encoder::Direction> first = world.direction_of(first_target_place);
  const std::optional<ics::encoder::Direction> second = world.direction_of(second_target_place);
  float64 angle = 0.0;
  if (first.has_value() && second.has_value())
  {
    const float64 turn = ics::encoder::wrap_pi(second->azimuth_rad - first->azimuth_rad);
    angle = std::hypot(turn * std::cos(first->elevation_rad), second->elevation_rad - first->elevation_rad);
  }
  return angle;
}

// Flies the two engagements for a station, the second hit at a time.
Outcome fly(const ics::cue::Site& station,
            const float64 hit_s,
            const int64 hit_ns)
{
  Station_world world(station, ics::handoff_test::two_engagements(first_hit_s, hit_s));
  const std::optional<ics::encoder::Direction> target = world.direction_of(first_target_place);
  EXPECT_TRUE(target.has_value());
  world.mount.point({target->azimuth_rad + off_azimuth, target->elevation_rad + off_elevation});
  Outcome outcome{};
  while (world.since() < end_ns)
  {
    static_cast<void>(world.tick());
    const Handoff_status status = world.scheduler().status();
    const bool tracking = world.scheduler().mode() == Mode::tracking;
    if (tracking && (status.plan.current == 0) && (outcome.first_tracked_s < 0.0))
    {
      outcome.first_tracked_s = world.seconds();
    }
    if (tracking && (status.plan.current == 1) && (outcome.second_tracked_s < 0.0))
    {
      outcome.second_tracked_s = world.seconds();
    }
    if (world.since() == first_hit_ns)
    {
      outcome.first_centred = tracking && world.centred(first_target_place);
      outcome.apart_rad = apart(world);
    }
    if (world.since() == hit_ns)
    {
      outcome.second_centred = tracking && world.centred(second_target_place);
    }
    outcome.last = status;
    outcome.tracking_at_end = tracking;
  }
  outcome.cued_at_end = world.scheduler().cue_at(world.now()).has_value();
  return outcome;
}

// The seconds from the start to a slot's approach.
float64 approach_s(const Slot& slot)
{
  const ics::UtcTime start = ics::UtcTime{} + ics::Duration(ics::handoff_test::start_ns);
  return std::chrono::duration<float64>(slot.approach.time - start).count();
}

}  // namespace

// JSF deviation D-5: GoogleTest's TEST and EXPECT macros fix the form of the
// test names and expand to code of GoogleTest's own (AV 50, 51 and 209).
TEST(HandoffReplay, EveryStationHandsOffWithinThreeSeconds)
{
  int32 number = 1;
  for (const ics::cue::Site& station : ics::handoff_test::stations)
  {
    const Outcome outcome = fly(station, second_hit_s, second_hit_ns);
    const Slot& first = outcome.last.plan.slots[0];
    const Slot& second = outcome.last.plan.slots[1];
    EXPECT_GT(outcome.first_tracked_s, 0.0);
    EXPECT_TRUE(outcome.first_centred);
    EXPECT_GT(outcome.second_tracked_s, first_hit_s);
    EXPECT_LE(outcome.second_tracked_s - first_hit_s, budget_s);
    EXPECT_TRUE(outcome.second_centred);
    EXPECT_TRUE(first.done);
    EXPECT_TRUE(second.done);
    EXPECT_NEAR(approach_s(first), first_hit_s, time_tolerance_s);
    EXPECT_NEAR(approach_s(second), second_hit_s, time_tolerance_s);
    EXPECT_LT(first.approach.miss_m, miss_tolerance_m);
    EXPECT_LT(second.approach.miss_m, miss_tolerance_m);
    EXPECT_FALSE(second.out_of_reach);
    EXPECT_EQ(outcome.last.plan.current, std::nullopt);
    EXPECT_EQ(outcome.last.counts.handoffs, 1);
    EXPECT_TRUE(outcome.tracking_at_end);
    EXPECT_FALSE(outcome.cued_at_end);
    std::cout << "station " << number << ": targets " << (outcome.apart_rad * per_degree)
              << " degrees apart; tracking the first at " << outcome.first_tracked_s << " s; hand-off in "
              << (outcome.second_tracked_s - first_hit_s) << " s; approaches predicted at " << approach_s(first)
              << " s and " << approach_s(second) << " s, missing by " << first.approach.miss_m << " m and "
              << second.approach.miss_m << " m\n";
    ++number;
  }
}

TEST(HandoffReplay, AnEngagementTwoSecondsAfterTheFirstIsFlagged)
{
  const Outcome outcome = fly(ics::handoff_test::stations[0], close_hit_s, close_hit_ns);
  const Slot& second = outcome.last.plan.slots[1];
  EXPECT_TRUE(outcome.last.plan.slots[0].done);
  EXPECT_TRUE(second.done);
  EXPECT_TRUE(second.too_close);
  EXPECT_FALSE(outcome.last.plan.slots[0].too_close);
}
