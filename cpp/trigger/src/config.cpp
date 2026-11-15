// config.cpp: the [trigger] table's schema (ICS-040).
// JSF deviation D-6: ics::config and ics::Duration come from .hpp headers.
#include <chrono>
#include <cstddef>
#include <string>
#include <vector>

#include <ics/common/units.hpp>
#include <ics/common/universal_types.h>
#include <ics/config/reader.hpp>
#include <ics/cue/config.h>
#include <ics/cue/extrapolator.h>
#include <ics/trigger/config.h>
#include <ics/trigger/controller.h>
#include <ics/trigger/generator.h>
#include <ics/trigger/link.h>
#include <ics/trigger/shot.h>

namespace
{

using ics::float64;
using ics::int32;
using ics::int64;

// The limits of each setting.
constexpr int64 max_port = 65535;
constexpr ics::Duration second = std::chrono::seconds(1);
constexpr ics::Duration min_timeout = std::chrono::milliseconds(1);
constexpr ics::Duration min_width = std::chrono::nanoseconds(10);
constexpr ics::Duration max_wait = std::chrono::seconds(3600);
constexpr ics::Duration max_horizon = std::chrono::seconds(600);
constexpr float64 min_level_v = 0.2;
constexpr float64 max_level_v = 15.0;
constexpr float64 min_speed_m_s = 0.1;
constexpr float64 max_speed_m_s = 100.0;

// The PPS and the margins around it.
ics::trigger::Shot_settings read_shot(ics::config::Reader& trigger)
{
  ics::trigger::Shot_settings shot{};
  shot.period = second;
  shot.offset = trigger.duration("pps_offset_ns", ics::Duration::zero(), second);
  shot.command_margin = trigger.duration("command_margin_ns", ics::Duration::zero(), second);
  shot.guard = trigger.duration("guard_ns", ics::Duration::zero(), second);
  return shot;
}

// The controller's settings.
ics::trigger::Trigger_settings read_controller(ics::config::Reader& trigger)
{
  ics::trigger::Trigger_settings settings{};
  settings.shot = read_shot(trigger);
  settings.lead = trigger.duration("lead_ns", ics::Duration::zero(), second);
  settings.recommit = trigger.duration("recommit_ns", ics::Duration::zero(), second);
  settings.predict_period = trigger.duration("predict_period_ns", ics::Duration::zero(), max_wait);
  settings.arm_timeout = trigger.duration("arm_timeout_ns", second, max_wait);
  settings.max_clock_bound = trigger.duration("max_clock_bound_ns", ics::Duration::zero(), second);
  settings.approach.launch_speed_m_s = trigger.number("launch_speed", min_speed_m_s, max_speed_m_s);
  settings.approach.horizon = trigger.duration("horizon_ns", second, max_horizon);
  return settings;
}

}  // namespace

namespace ics::trigger
{

Trigger_config read_trigger_settings(config::Reader& trigger, const cue::Cue_settings& cue)
{
  Trigger_config config{};
  config.link.address = trigger.text("address");
  config.link.port = static_cast<uint16>(trigger.integer("port", 1, max_port));
  config.link.timeout = trigger.duration("reply_timeout_ns", min_timeout, second);
  config.trigger = read_controller(trigger);
  config.trigger.cue = cue;
  const std::vector<std::string> channels = trigger.texts("channels", 1U, channels_length);
  config.trigger.channel_count = static_cast<int32>(channels.size());
  config.generator.count = config.trigger.channel_count;
  for (int32 place = 0; place < config.trigger.channel_count; ++place)
  {
    const std::size_t at = static_cast<std::size_t>(place);
    config.trigger.channels[at] = channels[at];
    config.generator.channels[at] = channel_number(channels[at]);
  }
  config.generator.width = trigger.duration("width_ns", min_width, second);
  config.generator.level_v = trigger.number("trigger_level", min_level_v, max_level_v);
  return config;
}

Trigger_config read_trigger_config(config::Reader& root)
{
  config::Reader cue = root.section("cue");
  const cue::Cue_settings cue_settings = cue::read_cue_settings(cue);
  config::Reader trigger = root.section("trigger");
  return read_trigger_settings(trigger, cue_settings);
}

}  // namespace ics::trigger
