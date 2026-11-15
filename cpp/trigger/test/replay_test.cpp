// replay_test.cpp: the trigger controller replaying two engagements against
// an emulated 577 (ICS-040). This is the issue's "done when": it fires within
// 1 ms of its prediction in replay.
//
// Two targets fly straight lines, and an interceptor launches at each. Their
// PLI comes as the MAVLink adapter gives it, each position off by GNSS noise
// and each report 100 ms late. The engage command for each pair comes before
// its interceptor launches. The controller steps every 10 ms, and drives the
// 577's driver over an emulated 577, which the PPS triggers each second.
#include <chrono>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <bench.h>
#include <emulated_577.h>
#include <flight.h>
#include <ics/common/error.hpp>
#include <ics/common/units.hpp>
#include <ics/common/universal_types.h>
#include <ics/frames/enu.hpp>
#include <ics/trigger/controller.h>
#include <ics/trigger/generator.h>
#include <ics/v1/pli.pb.h>
#include <ics/v1/trigger_event.pb.h>

namespace
{

using ics::float64;
using ics::int32;
using ics::int64;
using ics::trigger_test::at;
using ics::trigger_test::Flight;
using ics::trigger_test::Sample;
using ics::trigger_test::second_ns;
using ics::trigger_test::step_ns;
using ics::v1::TriggerEvent;

constexpr int64 millisecond_ns = 1000000;
constexpr int64 report_period_ns = 100 * millisecond_ns;
constexpr int64 latency_ns = 100 * millisecond_ns;
constexpr int64 first_engage_ns = 2 * second_ns;
constexpr int64 second_engage_ns = 9 * second_ns;
constexpr int64 end_ns = 15 * second_ns;
constexpr int64 noise_seed = 40;
constexpr int32 search_steps = 60;
constexpr float64 search_span_s = 0.5;
constexpr float64 first_meet_s = 8.4;
constexpr float64 second_meet_s = 13.7;
// The "done when": the pulse within this of the prediction it was timed by.
constexpr float64 done_when_ns = 1.0E6;
// How close to the true closest approach noisy PLI and a second's wait put the pulse.
constexpr float64 truth_tolerance_ns = 2.0E7;
constexpr ics::Duration width = std::chrono::microseconds(100);
constexpr ics::Duration recommit = std::chrono::microseconds(200);
constexpr float64 level_v = 2.5;

// The first pair: a target heading east, and an interceptor from the pad that passes 3 m under it.
constexpr Flight first_target{{-150.0, 120.0, 50.0}, {-49.2, 120.0, 50.0}, 0.0, first_meet_s, 0.0};
constexpr Flight first_interceptor{{0.0, 0.0, 2.0}, {-49.2, 120.0, 47.0}, 3.0, first_meet_s, 1.5};
// The second pair: a target heading south-west, and an interceptor that passes 2 m east of it.
constexpr Flight second_target{{180.0, 60.0, 80.0}, {70.0, -40.0, 70.0}, 0.0, second_meet_s, 0.0};
constexpr Flight second_interceptor{{10.0, 5.0, 2.0}, {72.0, -40.0, 70.0}, 9.3, second_meet_s, 1.0};

struct Vehicle
{
  std::string name;
  Flight flight;
};

// How far apart two flights are at a time since the start.
float64 separation_m(const Flight& target,
                     const Flight& interceptor,
                     const float64 seconds)
{
  const Sample one = ics::trigger_test::sample_of(target, seconds);
  const Sample other = ics::trigger_test::sample_of(interceptor, seconds);
  return std::hypot(one.position[0] - other.position[0],
                    one.position[1] - other.position[1],
                    one.position[2] - other.position[2]);
}

// When two flights that meet near a time are closest, in seconds since the
// start: a ternary search, as their separation has one minimum there.
float64 closest_s(const Flight& target,
                  const Flight& interceptor,
                  const float64 near_s)
{
  float64 low = near_s - search_span_s;
  float64 high = near_s + search_span_s;
  for (int32 step = 0; step < search_steps; ++step)
  {
    const float64 third = (high - low) / 3.0;
    if (separation_m(target, interceptor, low + third) < separation_m(target, interceptor, high - third))
    {
      high -= third;
    }
    else
    {
      low += third;
    }
  }
  return (low + high) / 2.0;
}

// A replay: the controller, the 577's driver and an emulated 577.
class Replay
{
public:
  Replay()
      : driver_(instrument, ics::trigger::Bnc577_settings{{1, 2}, 2, width, level_v}),
        range_(ics::trigger_test::range_frame())
  {
    ics::trigger::Trigger_settings settings = ics::trigger_test::bench_settings();
    settings.recommit = recommit;
    // Every step's prediction is reported, so the events show what each shot was timed by.
    settings.predict_period = ics::Duration::zero();
    EXPECT_TRUE(driver_.set_up().has_value());
    ics::Result<std::unique_ptr<ics::trigger::Controller>> made =
      ics::trigger::Controller::make(settings, driver_, sink);
    EXPECT_TRUE(made.has_value());
    controller = std::move(*made);
  }

  // The report of a vehicle at a time since the start, its position off by noise.
  ics::v1::PliRecord noisy_report(const Vehicle& vehicle, const int64 valid_ns)
  {
    const float64 seconds = static_cast<float64>(valid_ns) / ics::trigger_test::per_second;
    Sample sample = ics::trigger_test::sample_of(vehicle.flight, seconds);
    sample.position[0] += horizontal_(noise_);
    sample.position[1] += horizontal_(noise_);
    sample.position[2] += vertical_(noise_);
    return ics::trigger_test::record_of(range_, vehicle.name, sample, ics::trigger_test::start_ns + valid_ns);
  }

  // Runs from a time to another, both since the start: the PPS each second,
  // every vehicle's report each 100 ms and 100 ms late, and a step each 10 ms.
  void run(const std::vector<Vehicle>& vehicles,
           const int64 from_ns,
           const int64 to_ns)
  {
    for (int64 now_ns = from_ns; now_ns < to_ns; now_ns += step_ns)
    {
      if ((now_ns % second_ns) == 0)
      {
        instrument.edge(at(now_ns));
      }
      if ((now_ns % report_period_ns) == 0)
      {
        for (const Vehicle& vehicle : vehicles)
        {
          static_cast<void>(controller->take(noisy_report(vehicle, now_ns - latency_ns)));
        }
      }
      controller->step(at(now_ns), ics::trigger_test::good_clock);
    }
  }

  ics::trigger_test::Emulated_577 instrument;
  ics::trigger_test::Recording_sink sink;
  std::unique_ptr<ics::trigger::Controller> controller;

private:
  ics::trigger::Bnc577 driver_;
  ics::frames::EnuFrame range_;
  std::mt19937_64 noise_{static_cast<std::mt19937_64::result_type>(noise_seed)};
  std::normal_distribution<float64> horizontal_{0.0, ics::trigger_test::pli_horizontal_sigma_m};
  std::normal_distribution<float64> vertical_{0.0, ics::trigger_test::pli_vertical_sigma_m};
};

// The FIRED event of a sequence.
TriggerEvent fired_in(const ics::trigger_test::Recording_sink& sink, const std::string& sequence)
{
  TriggerEvent found;
  for (const TriggerEvent& event : sink.events)
  {
    if ((event.kind() == TriggerEvent::KIND_FIRED) && (event.sequence_id() == sequence))
    {
      found = event;
    }
  }
  return found;
}

// The last prediction a sequence reported up to a time.
TriggerEvent predicted_by(const ics::trigger_test::Recording_sink& sink,
                          const std::string& sequence,
                          const int64 utc_ns)
{
  TriggerEvent found;
  for (const TriggerEvent& event : sink.events)
  {
    const bool of_sequence = (event.kind() == TriggerEvent::KIND_PREDICTED) && (event.sequence_id() == sequence);
    if (of_sequence && (event.time_utc_ns() <= utc_ns))
    {
      found = event;
    }
  }
  return found;
}

// What a sequence's pulse was, and how far it was from what it should be.
struct Outcome
{
  // When the 577 gave the pulse on both channels, in UTC nanoseconds.
  int64 pulse_ns;
  // The pulse after the prediction the shot was timed by, and after the last
  // prediction before the shot stood.
  int64 from_timed_by_ns;
  int64 from_standing_ns;
};

// A sequence's pulse on both channels, the first of them at a place in the
// 577's list, against its FIRED event and its predictions.
Outcome outcome_of(const Replay& replay,
                   const std::string& sequence,
                   const int64 place)
{
  const ics::trigger_test::Pulse& first = replay.instrument.pulses[static_cast<std::size_t>(place)];
  const ics::trigger_test::Pulse& second = replay.instrument.pulses[static_cast<std::size_t>(place + 1)];
  const TriggerEvent fired = fired_in(replay.sink, sequence);
  EXPECT_EQ(fired.kind(), TriggerEvent::KIND_FIRED) << sequence;
  EXPECT_TRUE(fired.detail().empty()) << sequence;
  EXPECT_EQ(first.channel, 1) << sequence;
  EXPECT_EQ(second.channel, 2) << sequence;
  EXPECT_EQ(second.time, first.time) << sequence;
  const int64 pulse_ns = first.time.time_since_epoch().count();
  EXPECT_EQ(fired.time_utc_ns(), pulse_ns) << sequence;
  // The shot stood from a command margin before its edge, the whole second before the pulse.
  const int64 edge_ns = (pulse_ns / second_ns) * second_ns;
  const TriggerEvent standing =
    predicted_by(replay.sink, sequence, edge_ns - ics::trigger_test::bench_margin.count());
  EXPECT_EQ(standing.kind(), TriggerEvent::KIND_PREDICTED) << sequence;
  return Outcome{pulse_ns,
                 pulse_ns - fired.predicted_closest_approach_utc_ns(),
                 pulse_ns - standing.predicted_closest_approach_utc_ns()};
}

// Checks a sequence's pulse: within the "done when" of the prediction the
// shot was timed by and of the last prediction before the shot stood, and
// near the true closest approach.
void expect_fired(const Replay& replay,
                  const std::string& sequence,
                  const int64 place,
                  const float64 true_s)
{
  const Outcome outcome = outcome_of(replay, sequence, place);
  EXPECT_LE(std::abs(static_cast<float64>(outcome.from_timed_by_ns)), done_when_ns) << sequence;
  EXPECT_LE(std::abs(static_cast<float64>(outcome.from_standing_ns)), done_when_ns) << sequence;
  const float64 from_truth_ns = static_cast<float64>(outcome.pulse_ns - ics::trigger_test::start_ns) -
                                (true_s * ics::trigger_test::per_second);
  EXPECT_LE(std::abs(from_truth_ns), truth_tolerance_ns) << sequence;
  std::cout << sequence << ": pulse " << outcome.from_timed_by_ns << " ns from the prediction it was timed by, "
            << outcome.from_standing_ns << " ns from the last prediction before it stood, "
            << std::llround(from_truth_ns) << " ns from the true closest approach\n";
}

}  // namespace

// JSF deviation D-5: GoogleTest's TEST and EXPECT macros fix the form of the
// test names and expand to code of GoogleTest's own (AV 50, 51 and 209).
TEST(Replay, FiresWithinAMillisecondOfItsPredictionForEachEngagement)
{
  Replay replay;
  const std::vector<Vehicle> vehicles{{"uas-1", first_target},
                                      {"int-1", first_interceptor},
                                      {"uas-2", second_target},
                                      {"int-2", second_interceptor}};
  replay.run(vehicles, 0, first_engage_ns);
  ASSERT_TRUE(replay.controller->engage("uas-1", "int-1", at(first_engage_ns)).has_value());
  replay.run(vehicles, first_engage_ns, second_engage_ns);
  ASSERT_EQ(replay.controller->stage(), ics::trigger::Stage::idle);
  ASSERT_TRUE(replay.controller->engage("uas-2", "int-2", at(second_engage_ns)).has_value());
  replay.run(vehicles, second_engage_ns, end_ns);
  EXPECT_EQ(replay.controller->stage(), ics::trigger::Stage::idle);

  // One pulse a channel for each engagement, and no other.
  ASSERT_EQ(replay.instrument.pulses.size(), 4U);
  EXPECT_FALSE(replay.instrument.armed);
  EXPECT_EQ(replay.sink.count(TriggerEvent::KIND_ARMED), 2);
  EXPECT_EQ(replay.sink.count(TriggerEvent::KIND_FIRED), 2);
  EXPECT_EQ(replay.sink.count(TriggerEvent::KIND_DISARMED) + replay.sink.count(TriggerEvent::KIND_FAULT), 0);
  EXPECT_EQ(replay.controller->lost_events(), 0);
  expect_fired(replay, "sequence-1", 0, closest_s(first_target, first_interceptor, first_meet_s));
  expect_fired(replay, "sequence-2", 2, closest_s(second_target, second_interceptor, second_meet_s));
}
