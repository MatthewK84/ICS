// scpi.cpp: the 577's replies, and the numbers it is sent (ICS-040).
// JSF deviation D-6: ics::check and ics::Duration come from .hpp headers.
#include <cstddef>
#include <string>
#include <string_view>

#include <ics/common/check.hpp>
#include <ics/common/units.hpp>
#include <ics/common/universal_types.h>
#include <ics/trigger/scpi.h>

namespace
{

using ics::int32;
using ics::int64;

constexpr int64 nanoseconds_per_second = 1000000000;
constexpr int32 fraction_digits = 9;
constexpr char first_printable = ' ';
constexpr char last_printable = '~';
constexpr std::size_t refusal_length = 2U;

// A line without the carriage return and line feed that end it.
std::string_view without_ending(const std::string_view line)
{
  std::string_view text = line;
  if ((!text.empty()) && (text.back() == '\n'))
  {
    text.remove_suffix(1U);
  }
  if ((!text.empty()) && (text.back() == '\r'))
  {
    text.remove_suffix(1U);
  }
  return text;
}

// Whether text is not empty and holds only printable ASCII.
bool printable(const std::string_view text)
{
  bool clean = !text.empty();
  for (const char letter : text)
  {
    clean = clean && (letter >= first_printable) && (letter <= last_printable);
  }
  return clean;
}

}  // namespace

namespace ics::trigger
{

Reply parse_reply(const std::string_view line)
{
  const std::string_view text = without_ending(line);
  Reply reply{Reply_kind::malformed, 0, text};
  if (printable(text))
  {
    if (text == "ok")
    {
      reply.kind = Reply_kind::ok;
    }
    else if (text.front() != '?')
    {
      reply.kind = Reply_kind::value;
    }
    else if ((text.size() == refusal_length) && (text.back() >= '1') && (text.back() <= '9'))
    {
      reply.kind = Reply_kind::refused;
      reply.code = static_cast<int32>(text.back()) - static_cast<int32>('0');
    }
    else
    {
      // A question mark with no code after it is not a reply.
    }
  }
  return reply;
}

std::string seconds_text(const Duration span)
{
  const int64 nanoseconds = check(span >= Duration::zero()) ? span.count() : 0;
  const std::string fraction = std::to_string(nanoseconds % nanoseconds_per_second);
  const int32 zeros = fraction_digits - static_cast<int32>(fraction.size());
  return std::to_string(nanoseconds / nanoseconds_per_second) + "." +
         std::string(static_cast<std::size_t>(zeros), '0') + fraction;
}

}  // namespace ics::trigger
