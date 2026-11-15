// config.cpp: the [handoff] table's schema (ICS-039).
// JSF deviation D-6: ics::config, ics::Meters and ics::Duration come from .hpp headers.
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <string>
#include <vector>

#include <ics/common/units.hpp>
#include <ics/common/universal_types.h>
#include <ics/config/reader.hpp>
#include <ics/cue/config.h>
#include <ics/cue/extrapolator.h>
#include <ics/handoff/approach.h>
#include <ics/handoff/config.h>
#include <ics/handoff/queue.h>
#include <ics/handoff/scheduler.h>
#include <ics/handoff/slew.h>

namespace
{

using ics::float64;

// The limits of each setting.
constexpr ics::Duration max_gap = std::chrono::seconds(60);
constexpr ics::Duration max_wait = std::chrono::seconds(10);
constexpr ics::Duration min_horizon = std::chrono::seconds(1);
constexpr ics::Duration max_horizon = std::chrono::seconds(600);
constexpr float64 min_miss_m = 0.01;
constexpr float64 max_miss_m = 1000.0;
constexpr float64 min_speed_m_s = 0.1;
constexpr float64 max_speed_m_s = 100.0;
constexpr float64 min_rate_rad_s = 0.001;
constexpr float64 max_rate_rad_s = 3.0;
constexpr float64 max_acceleration_rad_s2 = 20.0;
constexpr float64 min_gain = 0.1;
constexpr float64 max_gain = 50.0;

// The queue's settings.
ics::handoff::Queue_settings read_queue(ics::config::Reader& handoff)
{
  ics::handoff::Queue_settings queue{};
  queue.min_gap = handoff.duration("min_gap_ns", ics::Duration::zero(), max_gap);
  queue.dwell = handoff.duration("dwell_ns", ics::Duration::zero(), max_wait);
  queue.margin = handoff.duration("margin_ns", ics::Duration::zero(), max_wait);
  queue.settle = handoff.duration("settle_ns", ics::Duration::zero(), max_wait);
  queue.engaged_miss_m = handoff.meters("engaged_miss_m", ics::Meters(min_miss_m), ics::Meters(max_miss_m)).value();
  return queue;
}

}  // namespace

namespace ics::handoff
{

Handoff_settings read_handoff_settings(config::Reader& handoff, const cue::Cue_settings& cue)
{
  Handoff_settings settings{};
  settings.cue = cue;
  const std::vector<std::string> targets = handoff.texts("targets", 1U, engagements_length);
  const std::vector<std::string> interceptors = handoff.texts("interceptors", 1U, engagements_length);
  // A list shorter than the other leaves names empty, which valid() refuses.
  settings.count = static_cast<int32>(std::max(targets.size(), interceptors.size()));
  for (int32 engagement = 0; engagement < settings.count; ++engagement)
  {
    const std::size_t place = static_cast<std::size_t>(engagement);
    Pairing& pairing = settings.pairings[place];
    pairing.target = (place < targets.size()) ? targets[place] : std::string();
    pairing.interceptor = (place < interceptors.size()) ? interceptors[place] : std::string();
  }
  settings.queue = read_queue(handoff);
  settings.approach.launch_speed_m_s = handoff.number("launch_speed", min_speed_m_s, max_speed_m_s);
  settings.approach.horizon = handoff.duration("horizon_ns", min_horizon, max_horizon);
  settings.slew.rate_rad_s = handoff.number("slew_rate", min_rate_rad_s, max_rate_rad_s);
  settings.slew.acceleration_rad_s2 = handoff.number("slew_acceleration", min_rate_rad_s, max_acceleration_rad_s2);
  settings.slew.gain = handoff.number("slew_gain", min_gain, max_gain);
  return settings;
}

Handoff_settings read_handoff_config(config::Reader& root)
{
  config::Reader cue = root.section("cue");
  const cue::Cue_settings cue_settings = cue::read_cue_settings(cue);
  config::Reader handoff = root.section("handoff");
  return read_handoff_settings(handoff, cue_settings);
}

}  // namespace ics::handoff
