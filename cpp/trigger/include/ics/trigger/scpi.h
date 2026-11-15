// scpi.h: the text a Berkeley Nucleonics 577 pulse generator exchanges with
// its host (ICS-040), as its user manual gives it. The host sends one command
// a line, ended by a carriage return and a line feed, and the 577 answers
// each with one line: "ok" for a command it took, "?" and a digit from 1 to 9
// for one it refused, or the value a query asked for.
#ifndef ICS_TRIGGER_SCPI_H
#define ICS_TRIGGER_SCPI_H

// JSF deviation D-6: ics::Result and ics::Duration come from .hpp headers.
#include <string>
#include <string_view>

#include <ics/common/error.hpp>
#include <ics/common/units.hpp>
#include <ics/common/universal_types.h>

namespace ics::trigger
{

// What a reply line is.
enum class Reply_kind : uint8
{
  // The command was taken.
  ok,
  // The command was refused: code says why, 1 to 9.
  refused,
  // A query's value: text holds it.
  value,
  // Not a reply the 577 gives: empty, or holding a character that is not printable ASCII.
  malformed
};

struct Reply
{
  Reply_kind kind;
  // The 577's error code for a refusal, 1 to 9; 0 otherwise.
  int32 code;
  // The line without its ending, a view of the line parsed.
  std::string_view text;
};

// A reply line, with or without its line ending.
[[nodiscard]] Reply parse_reply(const std::string_view line);

// A time span as the 577 takes a delay or a width: seconds, to the
// nanosecond, such as "0.123456789". A negative span fails a check and reads
// as zero.
[[nodiscard]] std::string seconds_text(const Duration span);

// A link to an instrument: one command out, one reply line back.
class Scpi_link
{
public:
  Scpi_link() = default;
  Scpi_link(const Scpi_link&) = delete;
  Scpi_link& operator=(const Scpi_link&) = delete;
  Scpi_link(Scpi_link&&) = delete;
  Scpi_link& operator=(Scpi_link&&) = delete;
  virtual ~Scpi_link() = default;

  // Sends a command and returns the line the instrument answers with, or
  // kUnavailable when it does not answer.
  [[nodiscard]] virtual Result<std::string> ask(const std::string& command) = 0;
};

}  // namespace ics::trigger

#endif  // ICS_TRIGGER_SCPI_H
