// bench.h: a controller on a bench for its tests (ICS-040): a generator and a
// sink that record what they are given, and a still target with an
// interceptor that closes on it along east and may change its speed once.
#ifndef ICS_TRIGGER_TEST_BENCH_H
#define ICS_TRIGGER_TEST_BENCH_H

#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <flight.h>
#include <ics/common/error.hpp>
#include <ics/common/units.hpp>
#include <ics/common/universal_types.h>
#include <ics/frames/enu.hpp>
#include <ics/trigger/approach.h>
#include <ics/trigger/controller.h>
#include <ics/trigger/events.h>
#include <ics/trigger/generator.h>
#include <ics/trigger/shot.h>
#include <ics/v1/pli.pb.h>
#include <ics/v1/trigger_event.pb.h>

namespace ics::trigger_test
{

inline constexpr int64 step_ns = 10000000;
inline constexpr int64 steps_per_report = 10;
inline constexpr int64 second_ns = 1000000000;
inline constexpr float64 bench_height_m = 30.0;
inline constexpr float64 bench_miss_m = 2.0;
inline constexpr float64 launch_speed_m_s = 3.0;
inline constexpr Duration bench_horizon = std::chrono::seconds(60);
inline constexpr Duration bench_margin = std::chrono::milliseconds(200);
inline constexpr Duration bench_guard = std::chrono::milliseconds(50);
inline constexpr Duration bench_recommit = std::chrono::milliseconds(5);
inline constexpr Duration bench_predict_period = std::chrono::milliseconds(100);
inline constexpr Duration bench_arm_timeout = std::chrono::seconds(30);
inline constexpr Duration bench_clock_bound = std::chrono::microseconds(100);
inline constexpr trigger::Clock_state good_clock{true, std::chrono::microseconds(1)};

// The settings the bench's controller has.
inline trigger::Trigger_settings bench_settings()
{
  trigger::Trigger_settings settings{};
  settings.cue = cue_settings();
  settings.approach = {launch_speed_m_s, bench_horizon};
  settings.shot = {std::chrono::seconds(1), Duration::zero(), bench_margin, bench_guard};
  settings.lead = Duration::zero();
  settings.recommit = bench_recommit;
  settings.predict_period = bench_predict_period;
  settings.arm_timeout = bench_arm_timeout;
  settings.max_clock_bound = bench_clock_bound;
  settings.channels[0] = "A";
  settings.channels[1] = "B";
  settings.channel_count = 2;
  return settings;
}

// A generator that records what it is asked, and refuses what it is set to.
class Fake_generator final : public trigger::Pulse_generator
{
public:
  Status program(const Duration delay) override
  {
    programmed.push_back(delay);
    return answer(program_fails);
  }

  Status arm() override
  {
    ++arms;
    return answer(arm_fails);
  }

  Status disarm() override
  {
    ++disarms;
    return answer(disarm_fails);
  }

  std::vector<Duration> programmed;
  int32 arms = 0;
  int32 disarms = 0;
  bool program_fails = false;
  bool arm_fails = false;
  bool disarm_fails = false;

private:
  static Status answer(const bool fails)
  {
    Status status{};
    if (fails)
    {
      status = fail(Error::kUnavailable);
    }
    return status;
  }
};

// A sink that keeps every event, or none.
class Recording_sink final : public trigger::Event_sink
{
public:
  Status put(const v1::TriggerEvent& event) override
  {
    Status status{};
    if (fails)
    {
      status = fail(Error::kUnwritable);
    }
    else
    {
      events.push_back(event);
    }
    return status;
  }

  // How many events of a kind it holds.
  [[nodiscard]] int64 count(const v1::TriggerEvent::Kind kind) const
  {
    int64 found = 0;
    for (const v1::TriggerEvent& event : events)
    {
      found += (event.kind() == kind) ? 1 : 0;
    }
    return found;
  }

  // The last event of a kind; an event of no kind when there is none.
  [[nodiscard]] v1::TriggerEvent last(const v1::TriggerEvent::Kind kind) const
  {
    v1::TriggerEvent found;
    for (const v1::TriggerEvent& event : events)
    {
      if (event.kind() == kind)
      {
        found = event;
      }
    }
    return found;
  }

  std::vector<v1::TriggerEvent> events;
  bool fails = false;
};

// An interceptor that closes on the target along east: how far west of it
// it starts, its speed, and the time and the speed of its one change.
struct Closing
{
  float64 gap_m;
  float64 speed_m_s;
  float64 changes_s;
  float64 changed_speed_m_s;
};

// Where a closing interceptor is, and how it moves, at a time since the start.
inline Sample interceptor_at(const Closing& closing, const float64 seconds)
{
  const bool changed = seconds > closing.changes_s;
  const float64 before = changed ? closing.changes_s : seconds;
  const float64 after = changed ? (seconds - closing.changes_s) : 0.0;
  const float64 east = (closing.speed_m_s * before) + (closing.changed_speed_m_s * after) - closing.gap_m;
  return Sample{{east, bench_miss_m, bench_height_m},
                {changed ? closing.changed_speed_m_s : closing.speed_m_s, 0.0, 0.0}};
}

class Bench
{
public:
  explicit Bench(const trigger::Trigger_settings& settings)
      : range_(range_frame())
  {
    Result<std::unique_ptr<trigger::Controller>> made = trigger::Controller::make(settings, generator, sink);
    EXPECT_TRUE(made.has_value());
    controller = std::move(*made);
  }

  // Gives the controller both vehicles' reports at a time since the start.
  void report(const Closing& closing, const int64 since_ns)
  {
    const float64 seconds = static_cast<float64>(since_ns) / per_second;
    const Sample target{{0.0, 0.0, bench_height_m}, {0.0, 0.0, 0.0}};
    const int64 valid_ns = start_ns + since_ns;
    static_cast<void>(controller->take(record_of(range_, "uas-1", target, valid_ns)));
    static_cast<void>(controller->take(record_of(range_, "int-1", interceptor_at(closing, seconds), valid_ns)));
  }

  // Steps the controller every 10 ms from a time to another, both since the
  // start, with the vehicles' reports every 100 ms.
  void run(const Closing& closing,
           const int64 from_ns,
           const int64 to_ns)
  {
    for (int64 step = from_ns / step_ns; (step * step_ns) <= to_ns; ++step)
    {
      if ((step % steps_per_report) == 0)
      {
        report(closing, step * step_ns);
      }
      controller->step(at(step * step_ns), clock);
    }
  }

  [[nodiscard]] const frames::EnuFrame& range() const
  {
    return range_;
  }

  Fake_generator generator;
  Recording_sink sink;
  std::unique_ptr<trigger::Controller> controller;
  trigger::Clock_state clock = good_clock;

private:
  frames::EnuFrame range_;
};

}  // namespace ics::trigger_test

#endif  // ICS_TRIGGER_TEST_BENCH_H
