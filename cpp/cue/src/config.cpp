// config.cpp: the [cue] table's schema (ICS-037).
// JSF deviation D-6: ics::config, ics::Degrees, ics::Meters and ics::Duration come from .hpp headers.
#include <array>
#include <chrono>
#include <string>

#include <ics/common/units.hpp>
#include <ics/common/universal_types.h>
#include <ics/config/reader.hpp>
#include <ics/cue/config.h>
#include <ics/cue/extrapolator.h>
#include <ics/cue/filter.h>
#include <ics/cue/measurement.h>
#include <ics/v1/common.pb.h>

namespace
{

using ics::float64;

// The limits of each setting.
constexpr float64 max_latitude_deg = 90.0;
constexpr float64 max_longitude_deg = 180.0;
constexpr float64 max_height_m = 10000.0;
constexpr float64 max_density = 1000.0;
constexpr float64 min_gate = 1.0;
constexpr float64 max_gate = 100.0;
constexpr float64 max_sigma = 100.0;
constexpr float64 min_sigma_m = 0.001;
constexpr float64 max_sigma_m = 1000.0;
constexpr ics::Duration max_latency = std::chrono::seconds(2);
constexpr ics::Duration min_history = std::chrono::milliseconds(100);
constexpr ics::Duration max_history = std::chrono::seconds(10);

// The roles a target's reports may carry.
constexpr std::array<ics::config::Choice<ics::v1::EntityRole>, 4> roles{
  {{"target", ics::v1::ENTITY_ROLE_TARGET},
   {"interceptor", ics::v1::ENTITY_ROLE_INTERCEPTOR},
   {"debris", ics::v1::ENTITY_ROLE_DEBRIS},
   {"other", ics::v1::ENTITY_ROLE_OTHER}}};

// A site given by three keys: name_latitude_deg, name_longitude_deg and name_height_m.
ics::cue::Site read_site(ics::config::Reader& cue, const std::string& name)
{
  const ics::Degrees latitude =
    cue.degrees(name + "_latitude_deg", ics::Degrees(-max_latitude_deg), ics::Degrees(max_latitude_deg));
  const ics::Degrees longitude =
    cue.degrees(name + "_longitude_deg", ics::Degrees(-max_longitude_deg), ics::Degrees(max_longitude_deg));
  const ics::Meters height = cue.meters(name + "_height_m", ics::Meters(-max_height_m), ics::Meters(max_height_m));
  return {latitude.value(), longitude.value(), height.value()};
}

// A fix type's sigmas, given by name_horizontal_m and name_vertical_m.
ics::cue::Fix_sigmas read_sigmas(ics::config::Reader& cue, const std::string& name)
{
  const ics::Meters horizontal = cue.meters(name + "_horizontal_m", ics::Meters(min_sigma_m), ics::Meters(max_sigma_m));
  const ics::Meters vertical = cue.meters(name + "_vertical_m", ics::Meters(min_sigma_m), ics::Meters(max_sigma_m));
  return {horizontal.value(), vertical.value()};
}

// The filter's settings.
ics::cue::Filter_settings read_filter(ics::config::Reader& cue)
{
  ics::cue::Filter_settings filter{};
  filter.noise.horizontal = cue.number("horizontal_jerk_psd", 0.0, max_density);
  filter.noise.vertical = cue.number("vertical_jerk_psd", 0.0, max_density);
  filter.gate = cue.number("gate", min_gate, max_gate);
  filter.history = cue.duration("history_ns", min_history, max_history);
  filter.coast_limit = cue.duration("coast_limit_ns", min_history, max_history);
  filter.capacity = static_cast<ics::int32>(cue.integer("capacity", 1, ics::cue::max_history));
  filter.initial_velocity_sigma_m_s = cue.number("initial_velocity_sigma", min_sigma_m, max_sigma);
  filter.initial_acceleration_sigma_m_s2 = cue.number("initial_acceleration_sigma", min_sigma_m, max_sigma);
  return filter;
}

// The sigmas of a record that carries none.
ics::cue::Noise_defaults read_defaults(ics::config::Reader& cue)
{
  ics::cue::Noise_defaults defaults{};
  defaults.two_dimensional = read_sigmas(cue, "two_dimensional");
  defaults.three_dimensional = read_sigmas(cue, "three_dimensional");
  defaults.dgnss = read_sigmas(cue, "dgnss");
  defaults.rtk_float = read_sigmas(cue, "rtk_float");
  defaults.rtk_fixed = read_sigmas(cue, "rtk_fixed");
  defaults.other = read_sigmas(cue, "other");
  defaults.velocity_sigma_m_s = cue.number("velocity_sigma", min_sigma_m, max_sigma);
  return defaults;
}

}  // namespace

namespace ics::cue
{

Cue_settings read_cue_settings(config::Reader& cue)
{
  Cue_settings settings{};
  settings.range_origin = read_site(cue, "range_origin");
  settings.station = read_site(cue, "station");
  settings.target_role = cue.choice("target_role", roles);
  settings.command_latency = cue.duration("command_latency_ns", Duration::zero(), max_latency);
  settings.filter = read_filter(cue);
  settings.defaults = read_defaults(cue);
  return settings;
}

Cue_settings read_replay_config(config::Reader& root)
{
  config::Reader cue = root.section("cue");
  return read_cue_settings(cue);
}

}  // namespace ics::cue
