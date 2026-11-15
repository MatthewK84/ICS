// emulated_577.h: a Berkeley Nucleonics 577 for the trigger's tests (ICS-040),
// as its user manual describes it. It takes the commands the driver sends,
// one a line, and answers "ok", or "?3" for a keyword it does not know. Its
// system timer is in single-shot mode and started by the PPS, and each
// channel in single-shot mode gives one pulse each time the 577 is armed: so
// the first edge after it is armed starts every delay, and later edges do
// nothing until it is armed again.
#ifndef ICS_TRIGGER_TEST_EMULATED_577_H
#define ICS_TRIGGER_TEST_EMULATED_577_H

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include <ics/common/error.hpp>
#include <ics/common/units.hpp>
#include <ics/common/universal_types.h>
#include <ics/trigger/generator.h>
#include <ics/trigger/scpi.h>

namespace ics::trigger_test
{

inline constexpr int64 nanoseconds_per_second = 1000000000;
inline constexpr int64 decimal = 10;
// A channel's commands start ":PULSE" and its number.
inline constexpr int64 prefix_length = 6;
// Channels 1 to 8, and place 0, which is not used.
inline constexpr std::size_t places = static_cast<std::size_t>(trigger::max_channels + 1);
inline constexpr std::string_view identity = "BNC,577-8C,31309,2.4.1-1.0.11";

// A pulse a channel gave.
struct Pulse
{
  int32 channel;
  UtcTime time;
};

// The text from a place on.
inline std::string from_place(const std::string& text, const int64 place)
{
  return text.substr(static_cast<std::size_t>(place));
}

// The number some decimal digits spell.
inline int64 number_of(const std::string& digits)
{
  int64 value = 0;
  for (const char digit : digits)
  {
    value = (value * decimal) + (static_cast<int64>(digit) - static_cast<int64>('0'));
  }
  return value;
}

// A number of seconds such as "0.123456789", as nanoseconds.
inline Duration span_of(const std::string& text)
{
  const int64 point = static_cast<int64>(text.find('.'));
  const int64 whole = number_of(text.substr(0U, static_cast<std::size_t>(point)));
  return Duration((whole * nanoseconds_per_second) + number_of(from_place(text, point + 1)));
}

class Emulated_577 final : public trigger::Scpi_link
{
public:
  // The reply to a command, without its line ending.
  std::string answer(const std::string& command)
  {
    commands.push_back(command);
    const int64 space = static_cast<int64>(command.find(' '));
    const std::string keyword = (space < 0) ? command : command.substr(0U, static_cast<std::size_t>(space));
    const std::string value = (space < 0) ? std::string() : from_place(command, space + 1);
    std::string reply = "ok";
    if ((!refused.empty()) && (command.rfind(refused, 0U) == 0U))
    {
      reply = "?5";
    }
    else if (command == "*IDN?")
    {
      reply = std::string(identity);
    }
    else if (keyword == ":PULSE0:STATE")
    {
      arm(value == "ON");
    }
    else if (keyword.rfind(":PULSE0:", 0U) == 0U)
    {
      // The system timer's mode and its trigger, which the driver sets one way.
    }
    else if (keyword.rfind(":PULSE", 0U) == 0U)
    {
      reply = set_channel(keyword, value);
    }
    else
    {
      reply = "?3";
    }
    return reply;
  }

  Result<std::string> ask(const std::string& command) override
  {
    Result<std::string> reply = fail(Error::kUnavailable);
    if (!silent)
    {
      reply = garbled ? std::string("\x01\r\n") : (answer(command) + "\r\n");
    }
    return reply;
  }

  // A PPS edge: armed, every channel in use that has not given its pulse
  // since the 577 was armed gives it, its delay after the edge.
  void edge(const UtcTime at)
  {
    for (int32 channel = 1; channel <= trigger::max_channels; ++channel)
    {
      Channel& state = channels_[static_cast<std::size_t>(channel)];
      if (armed && state.on && (!state.fired))
      {
        pulses.push_back(Pulse{channel, at + state.delay});
        state.fired = true;
      }
    }
  }

  // Every command taken, and every pulse given.
  std::vector<std::string> commands;
  std::vector<Pulse> pulses;
  bool armed = false;
  // It does not answer; it answers with a byte no reply holds; and it refuses commands that start like this.
  bool silent = false;
  bool garbled = false;
  std::string refused;

private:
  struct Channel
  {
    bool on = false;
    bool fired = false;
    Duration delay{};
  };

  void arm(const bool on)
  {
    armed = on;
    for (Channel& channel : channels_)
    {
      channel.fired = false;
    }
  }

  // Takes a channel's setting, such as ":PULSE1:DEL 0.25"; "?3" for a channel it has not got.
  std::string set_channel(const std::string& keyword, const std::string& value)
  {
    const int32 number =
      static_cast<int32>(keyword[static_cast<std::size_t>(prefix_length)]) - static_cast<int32>('0');
    std::string reply = "?3";
    if ((number >= 1) && (number <= trigger::max_channels))
    {
      reply = "ok";
      Channel& channel = channels_[static_cast<std::size_t>(number)];
      const std::string setting = from_place(keyword, prefix_length + 1);
      if (setting == ":STATE")
      {
        channel.on = value == "ON";
      }
      else if (setting == ":DEL")
      {
        channel.delay = span_of(value);
      }
      else
      {
        // Its mode, sync, polarity and width, which the driver sets one way.
      }
    }
    return reply;
  }

  std::array<Channel, places> channels_{};
};

}  // namespace ics::trigger_test

#endif  // ICS_TRIGGER_TEST_EMULATED_577_H
