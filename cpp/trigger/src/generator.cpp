// generator.cpp: the Berkeley Nucleonics 577 as the trigger's pulse generator (ICS-040).
// JSF deviation D-6: ics::check, ics::Status and ics::Duration come from .hpp headers.
#include <chrono>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

#include <ics/common/check.hpp>
#include <ics/common/error.hpp>
#include <ics/common/units.hpp>
#include <ics/common/universal_types.h>
#include <ics/trigger/generator.h>
#include <ics/trigger/scpi.h>

namespace
{

using ics::float64;
using ics::int32;
using ics::int64;

// What the 577 takes.
constexpr ics::Duration min_width = std::chrono::nanoseconds(10);
constexpr ics::Duration max_width = std::chrono::seconds(1);
constexpr float64 min_level_v = 0.2;
constexpr float64 max_level_v = 15.0;
constexpr float64 hundredths = 100.0;
constexpr int64 hundred = 100;
constexpr int64 ten = 10;

// A level in volts, to the hundredth, such as "2.50".
std::string level_text(const float64 level_v)
{
  const int64 parts = std::llround(level_v * hundredths);
  const int64 fraction = parts % hundred;
  return std::to_string(parts / hundred) + "." + std::to_string(fraction / ten) + std::to_string(fraction % ten);
}

// A channel's command prefix, such as ":PULSE1".
std::string channel_prefix(const int32 channel)
{
  return ":PULSE" + std::to_string(channel);
}

}  // namespace

namespace ics::trigger
{

int32 channel_number(const std::string& name)
{
  int32 number = 0;
  if ((name.size() == 1U) && (name.front() >= 'A') && (name.front() <= 'H'))
  {
    number = (static_cast<int32>(name.front()) - static_cast<int32>('A')) + 1;
  }
  return number;
}

bool valid(const Bnc577_settings& settings)
{
  bool channels = (settings.count >= 1) && (settings.count <= max_channels);
  for (int32 place = 0; channels && (place < settings.count); ++place)
  {
    const int32 channel = settings.channels[static_cast<std::size_t>(place)];
    channels = (channel >= 1) && (channel <= max_channels);
  }
  const bool wide = (settings.width >= min_width) && (settings.width <= max_width);
  return channels && wide && (settings.level_v >= min_level_v) && (settings.level_v <= max_level_v);
}

Bnc577::Bnc577(Scpi_link& link, const Bnc577_settings& settings)
    : link_(link), settings_(settings)
{
  if (!check(valid(settings)))
  {
    settings_.count = 0;
  }
}

Status Bnc577::send(const std::string& command)
{
  const Result<std::string> answer = link_.ask(command);
  Status status = fail(Error::kUnavailable);
  if (answer.has_value())
  {
    const Reply reply = parse_reply(*answer);
    if (reply.kind == Reply_kind::ok)
    {
      status = Status{};
    }
    else if (reply.kind == Reply_kind::refused)
    {
      status = fail(Error::kInvalidArgument);
    }
    else
    {
      status = fail(Error::kMalformed);
    }
  }
  return status;
}

Status Bnc577::send_all(const std::vector<std::string>& commands)
{
  Status status{};
  for (const std::string& command : commands)
  {
    if (status.has_value())
    {
      status = send(command);
    }
  }
  return status;
}

Status Bnc577::set_up()
{
  std::vector<std::string> commands{":PULSE0:STATE OFF",
                                    ":PULSE0:MODE SING",
                                    ":PULSE0:TRIG:MODE TRIG",
                                    ":PULSE0:TRIG:EDGE RIS",
                                    ":PULSE0:TRIG:LEV " + level_text(settings_.level_v)};
  for (int32 place = 0; place < settings_.count; ++place)
  {
    const std::string prefix = channel_prefix(settings_.channels[static_cast<std::size_t>(place)]);
    commands.push_back(prefix + ":CMODE SING");
    commands.push_back(prefix + ":SYNC T0");
    commands.push_back(prefix + ":POL NORM");
    commands.push_back(prefix + ":WIDT " + seconds_text(settings_.width));
    commands.push_back(prefix + ":STATE ON");
  }
  const Result<std::string> answer = link_.ask("*IDN?");
  Status status = fail(Error::kUnavailable);
  if (answer.has_value())
  {
    const Reply reply = parse_reply(*answer);
    status = fail(Error::kMalformed);
    if (reply.kind == Reply_kind::value)
    {
      identity_ = std::string(reply.text);
      status = send_all(commands);
    }
  }
  return status;
}

Status Bnc577::program(const Duration delay)
{
  std::vector<std::string> commands;
  for (int32 place = 0; place < settings_.count; ++place)
  {
    const std::string prefix = channel_prefix(settings_.channels[static_cast<std::size_t>(place)]);
    commands.push_back(prefix + ":DEL " + seconds_text(delay));
  }
  return send_all(commands);
}

Status Bnc577::arm()
{
  return send(":PULSE0:STATE ON");
}

Status Bnc577::disarm()
{
  return send(":PULSE0:STATE OFF");
}

const std::string& Bnc577::identity() const
{
  return identity_;
}

}  // namespace ics::trigger
