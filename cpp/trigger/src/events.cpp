// events.cpp: the trigger event file (ICS-040).
// JSF deviation D-6: ics::Result, ics::Status and ics::store::crc32c come from .hpp headers.
#include <cstddef>
#include <fstream>
#include <ios>
#include <iterator>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include <ics/common/error.hpp>
#include <ics/common/universal_types.h>
#include <ics/store/crc32c.hpp>
#include <ics/trigger/events.h>
#include <ics/v1/trigger_event.pb.h>

namespace
{

using ics::int32;
using ics::int64;
using ics::uint32;

constexpr int32 word_bytes = 4;
constexpr int32 byte_bits = 8;
constexpr uint32 byte_mask = 0xFFU;
constexpr int64 header_bytes = 8;

// The CRC-32C of some bytes.
uint32 crc_of(const std::string_view bytes)
{
  return ics::store::crc32c(std::as_bytes(std::span(bytes)));
}

// Appends a number as four bytes, the lowest first.
void put_word(const uint32 value, std::string& out)
{
  for (int32 byte = 0; byte < word_bytes; ++byte)
  {
    const uint32 shifted = value >> static_cast<uint32>(byte_bits * byte);
    out.push_back(static_cast<char>(shifted & byte_mask));
  }
}

// The number in the four bytes at a position, the lowest first.
uint32 word_at(const std::string_view bytes, const int64 position)
{
  uint32 value = 0U;
  for (int32 byte = 0; byte < word_bytes; ++byte)
  {
    const uint32 part = static_cast<ics::uint8>(bytes[static_cast<std::size_t>(position + byte)]);
    value |= part << static_cast<uint32>(byte_bits * byte);
  }
  return value;
}

// The event in an entry's payload, if its checksum and its message are sound.
bool event_in(const std::string_view payload,
              const uint32 crc,
              ics::v1::TriggerEvent& event)
{
  bool sound = crc_of(payload) == crc;
  if (sound)
  {
    sound = event.ParseFromArray(payload.data(), static_cast<int32>(payload.size()));
  }
  return sound;
}

}  // namespace

namespace ics::trigger
{

Status append_event(const v1::TriggerEvent& event, std::string& out)
{
  std::string payload;
  static_cast<void>(event.SerializeToString(&payload));
  Status status = fail(Error::kInvalidArgument);
  if (payload.size() <= max_event_bytes)
  {
    put_word(static_cast<uint32>(payload.size()), out);
    put_word(crc_of(payload), out);
    static_cast<void>(out.append(payload));
    status = Status{};
  }
  return status;
}

Events read_events(const std::string_view bytes)
{
  Events found{{}, bytes.empty()};
  const int64 size = static_cast<int64>(bytes.size());
  if (bytes.substr(0U, event_magic.size()) == event_magic)
  {
    int64 position = static_cast<int64>(event_magic.size());
    bool sound = true;
    while (sound && ((size - position) >= header_bytes))
    {
      const int64 length = static_cast<int64>(word_at(bytes, position));
      const uint32 crc = word_at(bytes, position + word_bytes);
      const int64 next = position + header_bytes + length;
      sound = (length <= static_cast<int64>(max_event_bytes)) && (next <= size);
      if (sound)
      {
        const std::size_t start = static_cast<std::size_t>(position + header_bytes);
        v1::TriggerEvent event;
        sound = event_in(bytes.substr(start, static_cast<std::size_t>(length)), crc, event);
        if (sound)
        {
          found.events.push_back(event);
          position = next;
        }
      }
    }
    found.whole = sound && (position == size);
  }
  return found;
}

Result<Events> load_events(const std::string& path)
{
  std::ifstream file(path, std::ios::binary);
  Result<Events> events = fail(Error::kUnreadable);
  if (file.is_open())
  {
    const std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    events = read_events(bytes);
  }
  return events;
}

Event_file::Event_file(std::ofstream&& file)
    : file_(std::move(file))
{
}

Result<std::unique_ptr<Event_file>> Event_file::create(const std::string& path)
{
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  Result<std::unique_ptr<Event_file>> made = fail(Error::kUnwritable);
  if (file.is_open())
  {
    made = std::unique_ptr<Event_file>(new Event_file(std::move(file)));
  }
  return made;
}

Status Event_file::put(const v1::TriggerEvent& event)
{
  entry_.clear();
  if (!started_)
  {
    static_cast<void>(entry_.append(event_magic));
  }
  Status status = append_event(event, entry_);
  if (status.has_value())
  {
    static_cast<void>(file_.write(entry_.data(), static_cast<std::streamsize>(entry_.size())));
    static_cast<void>(file_.flush());
    if (file_.good())
    {
      started_ = true;
    }
    else
    {
      status = fail(Error::kUnwritable);
    }
  }
  return status;
}

}  // namespace ics::trigger
