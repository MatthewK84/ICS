// controller_test.cpp: the trigger controller's sequences on a bench (ICS-040).
#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>

#include <gtest/gtest.h>

#include <bench.h>
#include <flight.h>
#include <ics/common/error.hpp>
#include <ics/common/units.hpp>
#include <ics/common/universal_types.h>
#include <ics/trigger/controller.h>
#include <ics/trigger/shot.h>
#include <ics/v1/pli.pb.h>
#include <ics/v1/trigger_event.pb.h>

namespace
{

using ics::float64;
using ics::int64;
using ics::trigger::Controller;
using ics::trigger::Stage;
using ics::trigger::Take;
using ics::trigger::Trigger_settings;
using ics::trigger_test::at;
using ics::trigger_test::Bench;
using ics::trigger_test::bench_settings;
using ics::trigger_test::Closing;
using ics::trigger_test::second_ns;
using ics::v1::TriggerEvent;

constexpr int64 millisecond_ns = 1000000;
constexpr int64 hit_ns = 7500 * millisecond_ns;
constexpr int64 window_opens_ns = 6050 * millisecond_ns;
constexpr int64 stands_from_ns = 6810 * millisecond_ns;
constexpr int64 change_ns = 6200 * millisecond_ns;
constexpr int64 late_engage_ns = 6850 * millisecond_ns;
constexpr int64 after_timeout_ns = 3 * second_ns;
constexpr int64 edge_7_ns = 7 * second_ns;
constexpr int64 edge_8_ns = 8 * second_ns;
constexpr int64 end_ns = 9 * second_ns;
constexpr int64 tolerance_ns = 10 * millisecond_ns;
constexpr int64 first_second_reports = 10;
constexpr float64 never_s = 1000.0;
constexpr float64 change_s = 6.2;
constexpr float64 speed_m_s = 20.0;
// 150 m at 20 m/s meets at 7.5 s; slowed at 6.2 s with 26 m to go, at 7.55 s.
constexpr float64 gap_m = 150.0;
constexpr float64 slowed_m_s = 19.2592592592592;
// 159 m at 20 m/s meets at 7.95 s; slowed at 6.2 s with 35 m to go, at 8.05 s.
constexpr float64 long_gap_m = 159.0;
constexpr float64 slower_m_s = 18.9189189189189;
constexpr float64 parked_m_s = 0.0;
constexpr float64 far_north_deg = 91.0;
constexpr ics::Duration short_timeout = std::chrono::seconds(2);
constexpr ics::Duration loose_bound = std::chrono::milliseconds(1);
constexpr ics::Duration negative = std::chrono::nanoseconds(-1);
constexpr ics::int32 too_many_channels = 9;
constexpr Closing steady{gap_m, speed_m_s, never_s, speed_m_s};
constexpr Closing slowing{gap_m, speed_m_s, change_s, slowed_m_s};
constexpr Closing falling_back{long_gap_m, speed_m_s, change_s, slower_m_s};
constexpr Closing parked{gap_m, parked_m_s, never_s, parked_m_s};

// A bench with a sequence armed at the start.
void engage(Bench& bench)
{
  ASSERT_TRUE(bench.controller->engage("uas-1", "int-1", at(0)).has_value());
}

int64 since_start(const int64 utc_ns)
{
  return utc_ns - ics::trigger_test::start_ns;
}

// Whether a bench's settings, changed, are refused.
bool refused(const Trigger_settings& settings)
{
  return ics::trigger::valid(settings).error() == ics::Error::kInvalidArgument;
}

}  // namespace

// JSF deviation D-5: GoogleTest's TEST and EXPECT macros fix the form of the
// test names and expand to code of GoogleTest's own (AV 50, 51 and 209).
TEST(Controller, FiresAtTheClosestApproachItPredicts)
{
  Bench bench(bench_settings());
  engage(bench);
  EXPECT_EQ(bench.controller->stage(), Stage::armed);
  EXPECT_FALSE(bench.controller->shot().has_value());
  bench.run(steady, 0, second_ns - 1);
  EXPECT_EQ(bench.sink.count(TriggerEvent::KIND_PREDICTED), first_second_reports);
  bench.run(steady, second_ns, window_opens_ns - 1);
  EXPECT_EQ(bench.controller->stage(), Stage::armed);
  EXPECT_TRUE(bench.generator.programmed.empty());
  bench.run(steady, window_opens_ns, hit_ns - tolerance_ns - 1);
  ASSERT_EQ(bench.controller->stage(), Stage::committed);
  const std::optional<ics::trigger::Shot> shot = bench.controller->shot();
  ASSERT_TRUE(shot.has_value());
  EXPECT_EQ(shot->edge, at(edge_7_ns));
  EXPECT_FALSE(shot->late);
  EXPECT_EQ(shot->delay, bench.generator.programmed.back());
  EXPECT_EQ(bench.generator.arms, 1);
  EXPECT_EQ(bench.generator.disarms, 0);
  bench.run(steady, hit_ns - tolerance_ns, end_ns);
  EXPECT_EQ(bench.controller->stage(), Stage::idle);
  EXPECT_EQ(bench.generator.arms, 1);
  EXPECT_EQ(bench.generator.disarms, 1);
  ASSERT_EQ(bench.sink.count(TriggerEvent::KIND_FIRED), 1);
  const TriggerEvent fired = bench.sink.last(TriggerEvent::KIND_FIRED);
  EXPECT_EQ(fired.time_utc_ns(), ics::trigger::fire_time(*shot).time_since_epoch().count());
  EXPECT_NEAR(static_cast<float64>(since_start(fired.time_utc_ns())),
              static_cast<float64>(hit_ns),
              static_cast<float64>(tolerance_ns));
  EXPECT_NEAR(static_cast<float64>(fired.predicted_closest_approach_utc_ns() - fired.time_utc_ns()),
              0.0,
              static_cast<float64>(tolerance_ns));
  EXPECT_NEAR(fired.predicted_miss_distance_m(), ics::trigger_test::bench_miss_m, 0.1);
  EXPECT_EQ(fired.sequence_id(), "sequence-1");
  EXPECT_EQ(fired.target_entity_id(), "uas-1");
  EXPECT_EQ(fired.interceptor_entity_id(), "int-1");
  ASSERT_EQ(fired.channels_size(), 2);
  EXPECT_EQ(fired.channels(0), "A");
  EXPECT_EQ(fired.channels(1), "B");
  EXPECT_TRUE(fired.detail().empty());
  EXPECT_EQ(bench.sink.events.front().kind(), TriggerEvent::KIND_ARMED);
  EXPECT_EQ(bench.sink.events.front().time_utc_ns(), ics::trigger_test::start_ns);
  EXPECT_EQ(bench.sink.events.back().kind(), TriggerEvent::KIND_FIRED);
  EXPECT_EQ(bench.sink.count(TriggerEvent::KIND_DISARMED) + bench.sink.count(TriggerEvent::KIND_FAULT), 0);
  EXPECT_EQ(bench.controller->lost_events(), 0);
}

TEST(Controller, ReportsItsPredictionsNoOftenerThanThePeriod)
{
  Bench bench(bench_settings());
  engage(bench);
  bench.run(steady, 0, window_opens_ns);
  int64 last_ns = 0;
  for (const TriggerEvent& event : bench.sink.events)
  {
    if (event.kind() == TriggerEvent::KIND_PREDICTED)
    {
      const int64 since_last_ns = event.time_utc_ns() - last_ns;
      EXPECT_TRUE((last_ns == 0) || (since_last_ns >= ics::trigger_test::bench_predict_period.count()));
      EXPECT_NEAR(static_cast<float64>(since_start(event.predicted_closest_approach_utc_ns())),
                  static_cast<float64>(hit_ns),
                  static_cast<float64>(tolerance_ns));
      EXPECT_TRUE(event.channels().empty());
      last_ns = event.time_utc_ns();
    }
  }
  EXPECT_GT(last_ns, 0);
}

TEST(Controller, ProgramsTheDelayAgainWhenThePredictionMovesOnTheSameEdge)
{
  Bench bench(bench_settings());
  engage(bench);
  bench.run(slowing, 0, change_ns);
  ASSERT_EQ(bench.controller->stage(), Stage::committed);
  const std::size_t before = bench.generator.programmed.size();
  const ics::Duration first = bench.generator.programmed.back();
  bench.run(slowing, change_ns + ics::trigger_test::step_ns, end_ns);
  EXPECT_GT(bench.generator.programmed.size(), before);
  EXPECT_GT(bench.generator.programmed.back() - first, std::chrono::milliseconds(30));
  EXPECT_EQ(bench.generator.arms, 1);
  EXPECT_EQ(bench.generator.disarms, 1);
  const TriggerEvent fired = bench.sink.last(TriggerEvent::KIND_FIRED);
  EXPECT_EQ(fired.time_utc_ns(), (at(edge_7_ns) + bench.generator.programmed.back()).time_since_epoch().count());
  EXPECT_NEAR(static_cast<float64>(since_start(fired.time_utc_ns())),
              static_cast<float64>(hit_ns + (50 * millisecond_ns)),
              static_cast<float64>(tolerance_ns));
}

TEST(Controller, AShotStandsFromTheCommandMarginBeforeItsEdge)
{
  Bench bench(bench_settings());
  engage(bench);
  bench.run(steady, 0, stands_from_ns);
  const std::size_t programmed = bench.generator.programmed.size();
  // The interceptor slows after the shot stands: the pulse stays where it is.
  const Closing slowing_late{gap_m, speed_m_s, 6.85, slowed_m_s};
  bench.run(slowing_late, stands_from_ns + ics::trigger_test::step_ns, end_ns);
  EXPECT_EQ(bench.generator.programmed.size(), programmed);
  EXPECT_EQ(bench.sink.count(TriggerEvent::KIND_FIRED), 1);
}

TEST(Controller, WithdrawsAShotWhoseEdgeThePredictionLeaves)
{
  Bench bench(bench_settings());
  engage(bench);
  bench.run(falling_back, 0, change_ns);
  ASSERT_EQ(bench.controller->stage(), Stage::committed);
  EXPECT_EQ(bench.controller->shot()->edge, at(edge_7_ns));
  bench.run(falling_back, change_ns + ics::trigger_test::step_ns, edge_7_ns);
  EXPECT_EQ(bench.controller->stage(), Stage::armed);
  EXPECT_EQ(bench.generator.disarms, 1);
  bench.run(falling_back, edge_7_ns + ics::trigger_test::step_ns, end_ns);
  EXPECT_EQ(bench.generator.arms, 2);
  EXPECT_EQ(bench.generator.disarms, 2);
  ASSERT_EQ(bench.sink.count(TriggerEvent::KIND_FIRED), 1);
  const TriggerEvent fired = bench.sink.last(TriggerEvent::KIND_FIRED);
  EXPECT_EQ(fired.time_utc_ns(), (at(edge_8_ns) + bench.generator.programmed.back()).time_since_epoch().count());
  EXPECT_NEAR(static_cast<float64>(since_start(fired.time_utc_ns())),
              static_cast<float64>(edge_8_ns + (50 * millisecond_ns)),
              static_cast<float64>(tolerance_ns));
  EXPECT_EQ(bench.sink.count(TriggerEvent::KIND_FAULT), 0);
}

TEST(Controller, AWithdrawalTheGeneratorRefusesIsAFault)
{
  Bench bench(bench_settings());
  engage(bench);
  bench.run(falling_back, 0, change_ns);
  ASSERT_EQ(bench.controller->stage(), Stage::committed);
  bench.generator.disarm_fails = true;
  bench.run(falling_back, change_ns + ics::trigger_test::step_ns, end_ns);
  EXPECT_EQ(bench.controller->stage(), Stage::idle);
  EXPECT_EQ(bench.generator.arms, 1);
  EXPECT_EQ(bench.generator.disarms, 1);
  EXPECT_EQ(bench.sink.count(TriggerEvent::KIND_FIRED), 0);
  ASSERT_EQ(bench.sink.count(TriggerEvent::KIND_FAULT), 1);
  EXPECT_EQ(bench.sink.events.back().detail(), "the pulse generator did not disarm");
}

TEST(Controller, AnEngagementTooLateForItsEdgeFiresOnTheNextAndSaysSo)
{
  Bench bench(bench_settings());
  ASSERT_TRUE(bench.controller->engage("uas-1", "int-1", at(late_engage_ns)).has_value());
  bench.run(steady, late_engage_ns, end_ns);
  EXPECT_EQ(bench.controller->stage(), Stage::idle);
  ASSERT_EQ(bench.generator.programmed.size(), 1U);
  EXPECT_EQ(bench.generator.programmed.front(), ics::Duration::zero());
  const TriggerEvent fired = bench.sink.last(TriggerEvent::KIND_FIRED);
  EXPECT_EQ(since_start(fired.time_utc_ns()), edge_8_ns);
  EXPECT_EQ(fired.detail(), "the pulse went out late");
}

TEST(Controller, DisarmsWhenTheClockIsNotLockedWithinItsBound)
{
  Bench unlocked(bench_settings());
  engage(unlocked);
  unlocked.run(steady, 0, second_ns);
  unlocked.clock = {false, ics::Duration::zero()};
  unlocked.run(steady, second_ns + ics::trigger_test::step_ns, 2 * second_ns);
  EXPECT_EQ(unlocked.controller->stage(), Stage::idle);
  ASSERT_EQ(unlocked.sink.count(TriggerEvent::KIND_DISARMED), 1);
  EXPECT_EQ(unlocked.sink.events.back().detail(), "the clock is not locked within its bound");
  EXPECT_EQ(unlocked.generator.disarms, 0);

  Bench loose(bench_settings());
  engage(loose);
  loose.run(steady, 0, change_ns);
  ASSERT_EQ(loose.controller->stage(), Stage::committed);
  loose.clock = {true, loose_bound};
  loose.run(steady, change_ns + ics::trigger_test::step_ns, end_ns);
  EXPECT_EQ(loose.controller->stage(), Stage::idle);
  ASSERT_EQ(loose.sink.count(TriggerEvent::KIND_DISARMED), 1);
  EXPECT_EQ(loose.sink.events.back().detail(), "the clock is not locked within its bound");
  EXPECT_EQ(loose.generator.disarms, 1);
  EXPECT_EQ(loose.sink.count(TriggerEvent::KIND_FIRED), 0);
}

TEST(Controller, DisarmsWhenNoPulseGoesOutBeforeTheArmingTimeout)
{
  Trigger_settings settings = bench_settings();
  settings.arm_timeout = short_timeout;
  Bench bench(settings);
  engage(bench);
  bench.run(parked, 0, 2 * second_ns);
  EXPECT_EQ(bench.controller->stage(), Stage::armed);
  EXPECT_EQ(bench.sink.count(TriggerEvent::KIND_PREDICTED), 0);
  bench.run(parked, (2 * second_ns) + ics::trigger_test::step_ns, after_timeout_ns);
  EXPECT_EQ(bench.controller->stage(), Stage::idle);
  ASSERT_EQ(bench.sink.count(TriggerEvent::KIND_DISARMED), 1);
  EXPECT_EQ(bench.sink.events.back().detail(), "no pulse went out before the arming timeout");
  EXPECT_EQ(since_start(bench.sink.events.back().time_utc_ns()), (2 * second_ns) + ics::trigger_test::step_ns);
}

TEST(Controller, AShotTheGeneratorRefusesIsAFault)
{
  Bench unprogrammed(bench_settings());
  engage(unprogrammed);
  unprogrammed.generator.program_fails = true;
  unprogrammed.run(steady, 0, end_ns);
  EXPECT_EQ(unprogrammed.controller->stage(), Stage::idle);
  EXPECT_EQ(unprogrammed.generator.programmed.size(), 1U);
  EXPECT_EQ(unprogrammed.generator.arms, 0);
  EXPECT_EQ(unprogrammed.generator.disarms, 1);
  ASSERT_EQ(unprogrammed.sink.count(TriggerEvent::KIND_FAULT), 1);
  EXPECT_EQ(unprogrammed.sink.events.back().detail(), "the pulse generator refused the shot");
  EXPECT_EQ(since_start(unprogrammed.sink.events.back().time_utc_ns()), window_opens_ns);
  EXPECT_EQ(unprogrammed.sink.count(TriggerEvent::KIND_FIRED), 0);

  Bench unarmed(bench_settings());
  engage(unarmed);
  unarmed.generator.arm_fails = true;
  unarmed.generator.disarm_fails = true;
  unarmed.run(steady, 0, end_ns);
  EXPECT_EQ(unarmed.controller->stage(), Stage::idle);
  EXPECT_EQ(unarmed.generator.arms, 1);
  ASSERT_EQ(unarmed.sink.count(TriggerEvent::KIND_FAULT), 2);
  EXPECT_EQ(unarmed.sink.events.back().detail(), "the pulse generator did not disarm");
  EXPECT_EQ(unarmed.sink.count(TriggerEvent::KIND_FIRED), 0);
}

TEST(Controller, AGeneratorThatDoesNotDisarmAfterItsPulseIsAFault)
{
  Bench bench(bench_settings());
  engage(bench);
  bench.generator.disarm_fails = true;
  bench.run(steady, 0, end_ns);
  EXPECT_EQ(bench.controller->stage(), Stage::idle);
  EXPECT_EQ(bench.sink.count(TriggerEvent::KIND_FIRED), 1);
  ASSERT_EQ(bench.sink.count(TriggerEvent::KIND_FAULT), 1);
  EXPECT_EQ(bench.sink.events.back().kind(), TriggerEvent::KIND_FAULT);
  EXPECT_EQ(bench.sink.events.back().detail(), "the pulse generator did not disarm");
}

TEST(Controller, CountsTheEventsItsSinkDoesNotKeep)
{
  Bench bench(bench_settings());
  bench.sink.fails = true;
  engage(bench);
  EXPECT_EQ(bench.controller->lost_events(), 1);
  bench.sink.fails = false;
  bench.run(steady, 0, end_ns);
  EXPECT_EQ(bench.controller->lost_events(), 1);
  EXPECT_EQ(bench.sink.count(TriggerEvent::KIND_ARMED), 0);
  EXPECT_EQ(bench.sink.count(TriggerEvent::KIND_FIRED), 1);
}

TEST(Controller, TheOperatorDisarmsASequenceAtAnyStage)
{
  Bench bench(bench_settings());
  bench.controller->disarm("not armed", at(0));
  EXPECT_TRUE(bench.sink.events.empty());
  engage(bench);
  bench.controller->disarm("range safety", at(second_ns));
  EXPECT_EQ(bench.controller->stage(), Stage::idle);
  ASSERT_EQ(bench.sink.count(TriggerEvent::KIND_DISARMED), 1);
  EXPECT_EQ(bench.sink.events.back().detail(), "range safety");
  EXPECT_EQ(bench.sink.events.back().sequence_id(), "sequence-1");
  EXPECT_EQ(bench.generator.disarms, 0);
  // Nothing is stepped while idle.
  bench.run(steady, 0, second_ns);
  EXPECT_EQ(bench.sink.count(TriggerEvent::KIND_PREDICTED), 0);

  engage(bench);
  bench.run(steady, 0, change_ns);
  ASSERT_EQ(bench.controller->stage(), Stage::committed);
  bench.controller->disarm("hold fire", at(change_ns));
  EXPECT_EQ(bench.controller->stage(), Stage::idle);
  EXPECT_EQ(bench.generator.disarms, 1);
  EXPECT_EQ(bench.sink.events.back().detail(), "hold fire");
  EXPECT_EQ(bench.sink.events.back().sequence_id(), "sequence-2");
  bench.run(steady, change_ns, end_ns);
  EXPECT_EQ(bench.sink.count(TriggerEvent::KIND_FIRED), 0);
}

TEST(Controller, TakesOnlyTheReportsOfItsSequencesVehicles)
{
  Bench bench(bench_settings());
  const ics::trigger_test::Sample here{{0.0, 0.0, ics::trigger_test::bench_height_m}, {0.0, 0.0, 0.0}};
  const ics::v1::PliRecord target =
    ics::trigger_test::record_of(bench.range(), "uas-1", here, ics::trigger_test::start_ns);
  EXPECT_EQ(bench.controller->take(target), Take::unlisted);
  engage(bench);
  EXPECT_EQ(bench.controller->take(target), Take::given);
  ics::v1::PliRecord interceptor = target;
  interceptor.set_entity_id("int-1");
  EXPECT_EQ(bench.controller->take(interceptor), Take::given);
  ics::v1::PliRecord other = target;
  other.set_entity_id("uas-2");
  EXPECT_EQ(bench.controller->take(other), Take::unlisted);
  ics::v1::PliRecord unfixed = target;
  unfixed.set_fix_type(ics::v1::PliRecord::FIX_TYPE_NONE);
  EXPECT_EQ(bench.controller->take(unfixed), Take::unusable);
}

TEST(Controller, PredictsNothingUntilBothVehiclesReport)
{
  Bench only_target(bench_settings());
  engage(only_target);
  const ics::trigger_test::Sample here{{0.0, 0.0, ics::trigger_test::bench_height_m}, {0.0, 0.0, 0.0}};
  static_cast<void>(only_target.controller->take(
    ics::trigger_test::record_of(only_target.range(), "uas-1", here, ics::trigger_test::start_ns)));
  only_target.controller->step(at(0), ics::trigger_test::good_clock);
  EXPECT_EQ(only_target.sink.count(TriggerEvent::KIND_PREDICTED), 0);

  Bench only_interceptor(bench_settings());
  engage(only_interceptor);
  static_cast<void>(only_interceptor.controller->take(
    ics::trigger_test::record_of(only_interceptor.range(), "int-1", here, ics::trigger_test::start_ns)));
  only_interceptor.controller->step(at(0), ics::trigger_test::good_clock);
  EXPECT_EQ(only_interceptor.sink.count(TriggerEvent::KIND_PREDICTED), 0);
  EXPECT_EQ(only_interceptor.controller->stage(), Stage::armed);
}

TEST(Controller, AnEngageCommandNeedsTwoVehiclesAndNoSequenceRunning)
{
  Bench bench(bench_settings());
  EXPECT_EQ(bench.controller->engage("", "int-1", at(0)).error(), ics::Error::kInvalidArgument);
  EXPECT_EQ(bench.controller->engage("uas-1", "", at(0)).error(), ics::Error::kInvalidArgument);
  EXPECT_EQ(bench.controller->engage("uas-1", "uas-1", at(0)).error(), ics::Error::kInvalidArgument);
  EXPECT_EQ(bench.controller->stage(), Stage::idle);
  EXPECT_TRUE(bench.sink.events.empty());
  engage(bench);
  EXPECT_EQ(bench.controller->engage("uas-2", "int-2", at(0)).error(), ics::Error::kFull);
  EXPECT_EQ(bench.sink.count(TriggerEvent::KIND_ARMED), 1);
}

TEST(Controller, IsMadeOnlyWithSettingsItCanUse)
{
  ics::trigger_test::Fake_generator generator;
  ics::trigger_test::Recording_sink sink;
  Trigger_settings off_the_earth = bench_settings();
  off_the_earth.cue.range_origin.latitude_deg = far_north_deg;
  EXPECT_EQ(Controller::make(off_the_earth, generator, sink).error(), ics::Error::kOutOfRange);
  Trigger_settings unnamed = bench_settings();
  unnamed.channel_count = 0;
  EXPECT_EQ(Controller::make(unnamed, generator, sink).error(), ics::Error::kInvalidArgument);
  EXPECT_TRUE(Controller::make(bench_settings(), generator, sink).has_value());
}

TEST(Controller, SettingsMustBeOnesAControllerCanUse)
{
  EXPECT_TRUE(ics::trigger::valid(bench_settings()).has_value());
  Trigger_settings settings = bench_settings();
  settings.channel_count = 0;
  EXPECT_TRUE(refused(settings));
  settings.channel_count = too_many_channels;
  EXPECT_TRUE(refused(settings));
  settings = bench_settings();
  settings.channels[1].clear();
  EXPECT_TRUE(refused(settings));
  settings = bench_settings();
  settings.lead = negative;
  EXPECT_TRUE(refused(settings));
  settings = bench_settings();
  settings.recommit = negative;
  EXPECT_TRUE(refused(settings));
  settings = bench_settings();
  settings.predict_period = negative;
  EXPECT_TRUE(refused(settings));
  settings = bench_settings();
  settings.arm_timeout = ics::Duration::zero();
  EXPECT_TRUE(refused(settings));
  settings = bench_settings();
  settings.max_clock_bound = negative;
  EXPECT_TRUE(refused(settings));
  settings = bench_settings();
  settings.approach.launch_speed_m_s = 0.0;
  EXPECT_TRUE(refused(settings));
  settings = bench_settings();
  settings.shot.guard = negative;
  EXPECT_TRUE(refused(settings));
}
