// generator.h: the pulse generator the trigger controller fires (ICS-040),
// and the Berkeley Nucleonics 577 as one.
//
// The 577 is set up once: its system timer in single-shot mode, started by
// its external trigger, the station's PPS; and each channel in use to give
// one pulse, of the width set, each time the 577 is armed. A shot then sets
// each channel's delay from the edge and arms the 577; the first edge after
// that starts the delay, and each channel puts its pulse out when its delay
// is over. Disarming stops the 577 before another edge can start it again.
//
// The 577 keeps a delay to 250 ps, and its own clock holds a delay within
// 1 ns plus a ten-thousandth of it: 0.1 ms over a second.
#ifndef ICS_TRIGGER_GENERATOR_H
#define ICS_TRIGGER_GENERATOR_H

// JSF deviation D-6: ics::Status and ics::Duration come from .hpp headers.
#include <array>
#include <cstddef>
#include <string>
#include <vector>

#include <ics/common/error.hpp>
#include <ics/common/units.hpp>
#include <ics/common/universal_types.h>
#include <ics/trigger/scpi.h>

namespace ics::trigger
{

// A pulse generator that the station's PPS triggers.
class Pulse_generator
{
public:
  Pulse_generator() = default;
  Pulse_generator(const Pulse_generator&) = delete;
  Pulse_generator& operator=(const Pulse_generator&) = delete;
  Pulse_generator(Pulse_generator&&) = delete;
  Pulse_generator& operator=(Pulse_generator&&) = delete;
  virtual ~Pulse_generator() = default;

  // Sets the delay from the next edge it is armed for to its pulse.
  [[nodiscard]] virtual Status program(const Duration delay) = 0;
  // Arms it: the next edge starts the delay.
  [[nodiscard]] virtual Status arm() = 0;
  // Disarms it: edges do nothing.
  [[nodiscard]] virtual Status disarm() = 0;
};

// The most channels a 577 has: A to H.
inline constexpr int32 max_channels = 8;
inline constexpr std::size_t channels_length = static_cast<std::size_t>(max_channels);

// The channel number of a name, 1 for "A" to 8 for "H"; 0 for any other text.
[[nodiscard]] int32 channel_number(const std::string& name);

struct Bnc577_settings
{
  // The channels in use, by number, and how many there are.
  std::array<int32, channels_length> channels;
  int32 count;
  // Each pulse's width.
  Duration width;
  // The level the PPS must cross to trigger, in volts.
  float64 level_v;
};

// Whether there are 1 to 8 channels numbered 1 to 8, a width from 10 ns to
// 1 s, and a level from 0.2 V to 15 V, as the 577 takes them.
[[nodiscard]] bool valid(const Bnc577_settings& settings);

// A 577, over a link that must outlive it.
class Bnc577 final : public Pulse_generator
{
public:
  // The settings must be valid(): a check fails otherwise.
  Bnc577(Scpi_link& link, const Bnc577_settings& settings);

  // Stops the 577 and sets it up. kUnavailable when it does not answer,
  // kMalformed for an answer that is not a reply, and kInvalidArgument for a
  // command it refuses.
  [[nodiscard]] Status set_up();
  // As set_up fails.
  [[nodiscard]] Status program(const Duration delay) override;
  [[nodiscard]] Status arm() override;
  [[nodiscard]] Status disarm() override;
  // What the 577 says it is, once set up.
  [[nodiscard]] const std::string& identity() const;

private:
  // Sends a command the 577 must take.
  [[nodiscard]] Status send(const std::string& command);
  // Sends commands until one fails.
  [[nodiscard]] Status send_all(const std::vector<std::string>& commands);

  Scpi_link& link_;
  Bnc577_settings settings_;
  std::string identity_;
};

}  // namespace ics::trigger

#endif  // ICS_TRIGGER_GENERATOR_H
