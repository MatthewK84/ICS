// scheduler_test.cpp: the scheduler's reports, cues, demands and refusals (ICS-039).
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <ics/common/error.hpp>
#include <ics/common/units.hpp>
#include <ics/common/universal_types.h>
#include <ics/cue/kinematics.h>
#include <ics/cue/pointing.h>
#include <ics/encoder/pointing_model.hpp>
#include <ics/frames/enu.hpp>
#include <ics/handoff/queue.h>
#include <ics/handoff/scheduler.h>
#include <ics/mount/pwi4_status.hpp>
#include <ics/mount/rate_loop.hpp>
#include <ics/testing/no_allocation_scope.hpp>
#include <ics/tracker/camera.h>
#include <ics/tracker/tracker.h>
#include <ics/v1/pli.pb.h>
#include <world.h>

namespace
{

using ics::float64;
using ics::int64;
using ics::handoff::Handoff_settings;
using ics::handoff::Handoff_status;
using ics::handoff::Scheduler;
using ics::handoff::Take;
using ics::handoff_test::first_target_place;
using ics::handoff_test::Station_world;
using ics::handoff_test::stations;
using ics::handoff_test::Vehicle;
using ics::tracker::Mode;

constexpr float64 first_hit_s = 8.0;
constexpr float64 second_hit_s = 11.0;
constexpr float64 late_hit_s = 14.0;
constexpr float64 early_hit_s = 9.0;
constexpr float64 off_azimuth = 0.1;
constexpr float64 off_elevation = -0.03;
constexpr float64 bad_latitude = 91.0;
constexpr float64 overhead_m = 30.0;
constexpr float64 never_s = 1000.0;
constexpr int64 tenth_ns = 100000000;
constexpr int64 slewing_ns = 400000000;
constexpr int64 tracking_ns = 3000000000;
constexpr int64 launched_ns = 6500000000;
constexpr int64 second_ns = 1000000000;
constexpr ics::Duration nap = std::chrono::microseconds(50);
constexpr std::size_t azimuth_axis = static_cast<std::size_t>(ics::mount::Axis::kAzimuth);

std::vector<Vehicle> vehicles()
{
  return ics::handoff_test::two_engagements(first_hit_s, second_hit_s);
}

Handoff_settings settings()
{
  return ics::handoff_test::settings_for(stations[0], vehicles());
}

// Why a scheduler cannot be made with these settings; nothing when it can.
std::optional<ics::Error> refusal(const Handoff_settings& handoff, const ics::tracker::Tracker_settings& tracking)
{
  const std::array<ics::encoder::Coefficient, 0> none{};
  const ics::Result<ics::encoder::PointingModel> model = ics::encoder::PointingModel::make(none);
  ics::handoff_test::Sim_mount mount;
  ics::handoff_test::Manual_time clock;
  const ics::Result<std::unique_ptr<Scheduler>> made = Scheduler::make(handoff, tracking, *model, mount, clock);
  std::optional<ics::Error> error;
  if (!made.has_value())
  {
    error = made.error();
  }
  return error;
}

std::optional<ics::Error> refusal(const Handoff_settings& handoff)
{
  return refusal(handoff, ics::handoff_test::tracking());
}

// A world whose mount starts off the first target by a tenth of a radian.
void point_near_first(Station_world& world)
{
  const std::optional<ics::encoder::Direction> target = world.direction_of(first_target_place);
  ASSERT_TRUE(target.has_value());
  world.mount.point({target->azimuth_rad + off_azimuth, target->elevation_rad + off_elevation});
}

// A record of the first target now.
ics::v1::PliRecord record_now(const Station_world& world, const std::string& name)
{
  const ics::frames::EnuFrame range(ics::handoff_test::point(ics::handoff_test::origin));
  const ics::handoff_test::Sample sample = ics::handoff_test::sample_of(vehicles()[0].flight, world.seconds());
  return ics::handoff_test::record_of(range, name, sample, ics::handoff_test::start_ns + world.since());
}

}  // namespace

// JSF deviation D-5: GoogleTest's TEST and EXPECT macros fix the form of the
// test names and expand to code of GoogleTest's own (AV 50, 51 and 209).
TEST(Scheduler, IsMadeForARunPlanOfNamedPairs)
{
  EXPECT_EQ(refusal(settings()), std::nullopt);
  Handoff_settings none = settings();
  none.count = 0;
  EXPECT_EQ(refusal(none), ics::Error::kInvalidArgument);
  Handoff_settings crowded = settings();
  crowded.count = ics::handoff::max_engagements + 1;
  EXPECT_EQ(refusal(crowded), ics::Error::kInvalidArgument);
  Handoff_settings unnamed = settings();
  unnamed.pairings[1].target.clear();
  EXPECT_EQ(refusal(unnamed), ics::Error::kInvalidArgument);
  Handoff_settings unmatched = settings();
  unmatched.pairings[1].interceptor.clear();
  EXPECT_EQ(refusal(unmatched), ics::Error::kInvalidArgument);
  Handoff_settings same = settings();
  same.pairings[0].interceptor = same.pairings[0].target;
  EXPECT_EQ(refusal(same), ics::Error::kInvalidArgument);
}

TEST(Scheduler, RefusesSettingsItCannotSteerBy)
{
  Handoff_settings approach = settings();
  approach.approach.launch_speed_m_s = 0.0;
  EXPECT_EQ(refusal(approach), ics::Error::kInvalidArgument);
  Handoff_settings queue = settings();
  queue.queue.dwell = std::chrono::nanoseconds(-1);
  EXPECT_EQ(refusal(queue), ics::Error::kInvalidArgument);
  Handoff_settings slew = settings();
  slew.slew.gain = 0.0;
  EXPECT_EQ(refusal(slew), ics::Error::kInvalidArgument);
  Handoff_settings latency = settings();
  latency.cue.command_latency = std::chrono::nanoseconds(-1);
  EXPECT_EQ(refusal(latency), ics::Error::kInvalidArgument);
  ics::tracker::Tracker_settings tracking = ics::handoff_test::tracking();
  tracking.gain = -1.0;
  EXPECT_EQ(refusal(settings(), tracking), ics::Error::kInvalidArgument);
}

TEST(Scheduler, RefusesSitesOffTheEarth)
{
  Handoff_settings origin = settings();
  origin.cue.range_origin.latitude_deg = bad_latitude;
  EXPECT_EQ(refusal(origin), ics::Error::kOutOfRange);
  Handoff_settings station = settings();
  station.cue.station.latitude_deg = bad_latitude;
  EXPECT_EQ(refusal(station), ics::Error::kOutOfRange);
}

TEST(Scheduler, GivesEachReportToTheVehicleItNames)
{
  Station_world world(stations[0], vehicles());
  EXPECT_EQ(world.scheduler().take(record_now(world, "stranger")), Take::unlisted);
  ics::v1::PliRecord blind = record_now(world, "uas-1");
  blind.set_fix_type(ics::v1::PliRecord::FIX_TYPE_NONE);
  EXPECT_EQ(world.scheduler().take(blind), Take::unusable);
  EXPECT_EQ(world.scheduler().take(record_now(world, "uas-1")), Take::given);
  const Handoff_status status = world.scheduler().status();
  EXPECT_EQ(status.counts.given, 1);
  EXPECT_EQ(status.counts.unlisted, 1);
  EXPECT_EQ(status.counts.unusable, 1);
}

TEST(Scheduler, HasNoCueAndNoDemandBeforeAReport)
{
  Station_world world(stations[0], vehicles());
  world.reporting = false;
  static_cast<void>(world.tick());
  EXPECT_EQ(world.scheduler().cue_at(world.now()).error(), ics::Error::kOutOfRange);
  EXPECT_EQ(world.scheduler().latest(), std::nullopt);
  const Handoff_status status = world.scheduler().status();
  EXPECT_EQ(status.plan.current, 0);
  EXPECT_FALSE(status.slewing);
  EXPECT_EQ(world.scheduler().mode(), Mode::cueing);
  EXPECT_EQ(world.scheduler().tracker_counts().frames, 1);
}

TEST(Scheduler, CuesATargetWhoseInterceptorHasNotReported)
{
  Station_world world(stations[0], vehicles());
  world.reporting = false;
  EXPECT_EQ(world.scheduler().take(record_now(world, "uas-1")), Take::given);
  static_cast<void>(world.tick());
  EXPECT_TRUE(world.scheduler().cue_at(world.now()).has_value());
  const Handoff_status status = world.scheduler().status();
  EXPECT_TRUE(status.slewing);
  EXPECT_FALSE(status.plan.slots[0].timed);
}

TEST(Scheduler, SlewsToTheTargetThenLeavesTheMountToTheTracker)
{
  Station_world world(stations[0], vehicles());
  point_near_first(world);
  world.run_until(slewing_ns);
  EXPECT_TRUE(world.scheduler().status().slewing);
  const std::optional<ics::mount::RateDemand> slew = world.scheduler().latest();
  ASSERT_TRUE(slew.has_value());
  EXPECT_DOUBLE_EQ(slew->rates.rad_per_s[azimuth_axis], -ics::handoff_test::slew.rate_rad_s);
  world.run_until(tracking_ns);
  EXPECT_EQ(world.scheduler().mode(), Mode::tracking);
  EXPECT_FALSE(world.scheduler().status().slewing);
  EXPECT_TRUE(world.scheduler().latest().has_value());
  EXPECT_TRUE(world.centred(first_target_place));
}

TEST(Scheduler, ServesTheEngagementWhoseInterceptorLaunchesFirst)
{
  Station_world world(stations[0], ics::handoff_test::two_engagements(late_hit_s, early_hit_s));
  point_near_first(world);
  world.run_until(tracking_ns);
  EXPECT_EQ(world.scheduler().status().plan.current, 0);
  EXPECT_EQ(world.scheduler().mode(), Mode::tracking);
  world.run_until(launched_ns);
  const Handoff_status status = world.scheduler().status();
  EXPECT_EQ(status.plan.current, 1);
  EXPECT_EQ(status.plan.order[0], 1);
  EXPECT_EQ(status.counts.handoffs, 1);
}

TEST(Scheduler, HasNoCueForATargetOverhead)
{
  const ics::frames::EnuFrame range(ics::handoff_test::point(ics::handoff_test::origin));
  const ics::cue::Vector3 station =
    ics::cue::station_at(range, ics::handoff_test::point(stations[0])).position;
  const ics::cue::Vector3 above{station[0], station[1], station[2] + overhead_m};
  const ics::cue::Vector3 higher{station[0], station[1], station[2] + overhead_m + 1.0};
  const std::vector<Vehicle> hovering{
    {"uas-1", {above, higher, 0.0, never_s, 0.0}, true},
    {"int-1", {ics::handoff_test::first_launch, above, never_s, never_s + 1.0, 1.0}, true}};
  Station_world world(stations[0], hovering);
  world.noisy = false;
  world.run_until(second_ns);
  EXPECT_EQ(world.scheduler().cue_at(world.now()).error(), ics::Error::kUnconstrained);
  EXPECT_FALSE(world.scheduler().status().slewing);
  EXPECT_FALSE(world.scheduler().status().plan.slots[0].timed);
}

TEST(Scheduler, LeavesTheMountAloneWhenItsAxesAreUnknown)
{
  Station_world world(stations[0], vehicles());
  point_near_first(world);
  world.mount.known = false;
  world.run_until(second_ns);
  EXPECT_TRUE(world.scheduler().cue_at(world.now()).has_value());
  EXPECT_FALSE(world.scheduler().status().slewing);
  EXPECT_EQ(world.scheduler().latest(), std::nullopt);
}

TEST(Scheduler, BringsTheQueueUpOnceForATime)
{
  Station_world world(stations[0], vehicles());
  point_near_first(world);
  world.run_until(tenth_ns);
  const ics::tracker::Frame again = world.frame();
  static_cast<void>(world.scheduler().take(again));
  const Handoff_status status = world.scheduler().status();
  EXPECT_EQ(status.plan.current, 0);
  EXPECT_EQ(status.counts.handoffs, 0);
}

TEST(Scheduler, ServesTheCameraTheReportsAndTheRateLoopAtOnce)
{
  Station_world world(stations[0], vehicles());
  point_near_first(world);
  const ics::v1::PliRecord record = record_now(world, "uas-2");
  std::atomic<bool> running{true};
  std::atomic<int64> asked{0};
  std::thread rate_loop(
    [&world, &running, &asked]()
    {
      while (running.load())
      {
        static_cast<void>(world.scheduler().latest());
        static_cast<void>(world.scheduler().status());
        static_cast<void>(world.scheduler().mode());
        ++asked;
        std::this_thread::sleep_for(nap);
      }
    });
  std::thread reports(
    [&world, &running, &record]()
    {
      while (running.load())
      {
        static_cast<void>(world.scheduler().take(record));
        std::this_thread::sleep_for(nap);
      }
    });
  world.run_until(tracking_ns);
  running.store(false);
  rate_loop.join();
  reports.join();
  EXPECT_GT(asked.load(), 0);
  EXPECT_EQ(world.scheduler().mode(), Mode::tracking);
  EXPECT_TRUE(world.centred(first_target_place));
}

TEST(Scheduler, AllocatesNothingOnceMade)
{
  Station_world world(stations[0], vehicles());
  point_near_first(world);
  world.run_until(tracking_ns);
  const ics::v1::PliRecord record = record_now(world, "uas-1");
  const ics::tracker::Frame frame = world.next_frame();
  {
    const ics::testing::NoAllocationScope no_allocation;
    EXPECT_EQ(world.scheduler().take(record), Take::given);
    EXPECT_EQ(world.scheduler().take(frame), ics::tracker::Take::detected);
    EXPECT_TRUE(world.scheduler().latest().has_value());
    EXPECT_EQ(world.scheduler().status().plan.current, 0);
  }
}
