// events.h: where the trigger controller's events go (ICS-040), and the
// trigger event file.
//
// The file is the trigger's own, apart from the PLI segment log (ICS-030). It
// starts with the 8-byte magic "ICSTRG1\n", followed by entries, each:
//
//   u32 payload length, little-endian
//   u32 CRC-32C of the payload, little-endian
//   the serialized ics.v1.TriggerEvent
//
// Entries are only appended, and each is handed to the operating system as
// it is put. The magic is written with the first event, so a file with no
// events is empty. A crash can leave the last entry part written: a reader
// stops at the first entry that is incomplete or fails its checks, and
// everything before it is sound.
#ifndef ICS_TRIGGER_EVENTS_H
#define ICS_TRIGGER_EVENTS_H

// JSF deviation D-6: ics::Result and ics::Status come from error.hpp.
#include <cstddef>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <ics/common/error.hpp>
#include <ics/v1/trigger_event.pb.h>

namespace ics::trigger
{

// Where events go.
class Event_sink
{
public:
  Event_sink() = default;
  Event_sink(const Event_sink&) = delete;
  Event_sink& operator=(const Event_sink&) = delete;
  Event_sink(Event_sink&&) = delete;
  Event_sink& operator=(Event_sink&&) = delete;
  virtual ~Event_sink() = default;

  // Takes an event; an error when it could not be kept.
  [[nodiscard]] virtual Status put(const v1::TriggerEvent& event) = 0;
};

inline constexpr std::string_view event_magic = "ICSTRG1\n";
// The largest payload an entry holds. An event is well under 1 KiB.
inline constexpr std::size_t max_event_bytes = 65536U;

// Appends an event's entry to out. kInvalidArgument when the event is larger
// than max_event_bytes, leaving out as it was.
[[nodiscard]] Status append_event(const v1::TriggerEvent& event, std::string& out);

// What a trigger event file holds.
struct Events
{
  // The events of every sound entry, in the file's order.
  std::vector<v1::TriggerEvent> events;
  // Whether the file is empty, or the magic and nothing but sound entries.
  bool whole;
};

// The events in a trigger event file's bytes.
[[nodiscard]] Events read_events(const std::string_view bytes);

// The events in a trigger event file; kUnreadable when it cannot be opened.
[[nodiscard]] Result<Events> load_events(const std::string& path);

// A trigger event file being written.
class Event_file final : public Event_sink
{
public:
  // A new file at path, replacing any there; kUnwritable when it cannot be opened.
  [[nodiscard]] static Result<std::unique_ptr<Event_file>> create(const std::string& path);

  // kUnwritable when the entry cannot be written, and as append_event fails.
  [[nodiscard]] Status put(const v1::TriggerEvent& event) override;

private:
  explicit Event_file(std::ofstream&& file);

  std::ofstream file_;
  std::string entry_;
  bool started_ = false;
};

}  // namespace ics::trigger

#endif  // ICS_TRIGGER_EVENTS_H
