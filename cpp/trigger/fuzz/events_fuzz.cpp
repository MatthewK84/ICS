// events_fuzz.cpp: fuzzes the trigger event file's reader (ICS-040) with any
// bytes, as a crash or a bad disk may leave them. It may not overflow or
// crash, and what it reads must keep its promises:
// - no more events than the bytes have room for, and
// - the events it finds, written to a new file, are read back whole and as many.
#include <cstddef>
#include <cstdlib>
#include <span>
#include <string>

#include <ics/common/universal_types.h>
#include <ics/trigger/events.h>
#include <ics/v1/trigger_event.pb.h>

namespace
{

void require(const bool condition)
{
  if (!condition)
  {
    std::abort();
  }
}

}  // namespace

// JSF deviation D-5: libFuzzer fixes the entry point's name and its parameters (AV 51).
extern "C" ics::int32 LLVMFuzzerTestOneInput(const ics::uint8* data, std::size_t size)
{
  const std::span<const ics::uint8> input(data, size);
  const std::string bytes(input.begin(), input.end());
  const ics::trigger::Events read = ics::trigger::read_events(bytes);
  std::string again(ics::trigger::event_magic);
  for (const ics::v1::TriggerEvent& event : read.events)
  {
    require(ics::trigger::append_event(event, again).has_value());
  }
  const ics::trigger::Events reread = ics::trigger::read_events(again);
  require(reread.whole);
  require(reread.events.size() == read.events.size());
  require(read.events.size() <= bytes.size());
  return 0;
}
