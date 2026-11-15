// reply_fuzz.cpp: fuzzes the 577's reply parser (ICS-040) with any bytes, as
// a link could deliver them. It may not overflow or crash, and what it reads
// must keep its promises:
// - the text is the line without its ending, and lies within the line,
// - a refusal's code is 1 to 9, and any other reply's is 0, and
// - a reply that is not malformed holds only printable ASCII.
#include <cstddef>
#include <cstdlib>
#include <span>
#include <string>
#include <string_view>

#include <ics/common/universal_types.h>
#include <ics/trigger/scpi.h>

namespace
{

constexpr ics::int32 max_code = 9;
constexpr char first_printable = ' ';
constexpr char last_printable = '~';

void require(const bool condition)
{
  if (!condition)
  {
    std::abort();
  }
}

// Whether every character is printable ASCII.
bool printable(const std::string_view text)
{
  bool all = true;
  for (const char character : text)
  {
    all = all && (character >= first_printable) && (character <= last_printable);
  }
  return all;
}

}  // namespace

// JSF deviation D-5: libFuzzer fixes the entry point's name and its parameters (AV 51).
extern "C" ics::int32 LLVMFuzzerTestOneInput(const ics::uint8* data, std::size_t size)
{
  const std::span<const ics::uint8> input(data, size);
  const std::string line(input.begin(), input.end());
  const ics::trigger::Reply reply = ics::trigger::parse_reply(line);
  require(reply.text.size() <= line.size());
  require(line.compare(0U, reply.text.size(), reply.text) == 0);
  const bool refused = reply.kind == ics::trigger::Reply_kind::refused;
  require(refused ? ((reply.code >= 1) && (reply.code <= max_code)) : (reply.code == 0));
  require((reply.kind == ics::trigger::Reply_kind::malformed) || ((!reply.text.empty()) && printable(reply.text)));
  return 0;
}
