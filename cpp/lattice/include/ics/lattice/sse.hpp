#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace ics::lattice {

// Server-sent events (ICS-023): the text/event-stream format Lattice streams
// entities in, as the HTML standard defines it ("Server-sent events",
// "Parsing an event stream"). Lines end with CRLF, LF or CR; "data:" lines
// accumulate, "event:" names the event, lines starting with ":" are comments,
// other fields such as "id:" and "retry:" are ignored, and a blank line
// dispatches the event. An event with no data is not dispatched.

struct SseEvent {
  // The "event:" field: empty when the event named none.
  std::string event;
  // The "data:" lines, joined by line feeds.
  std::string data;
};

// Reads a stream incrementally, as it arrives in chunks of any size.
class SseReader {
 public:
  // An event, or a line, longer than this many bytes is dropped, so a stream
  // cannot grow the reader without bound.
  static constexpr std::size_t kDefaultMaxEventBytes = std::size_t{4} << 20U;

  explicit SseReader(std::size_t max_event_bytes = kDefaultMaxEventBytes) noexcept;

  // The events the chunk completes.
  [[nodiscard]] std::vector<SseEvent> feed(std::string_view chunk);

  // Events dropped for passing the limit.
  [[nodiscard]] std::size_t oversized() const noexcept { return oversized_; }

 private:
  void take(char c, std::vector<SseEvent>& out);
  void end_line(std::vector<SseEvent>& out);
  void take_field(std::string_view line);
  void dispatch(std::vector<SseEvent>& out);

  std::size_t max_event_bytes_;
  std::string line_;
  SseEvent pending_;
  bool has_data_ = false;
  bool too_long_ = false;
  bool after_cr_ = false;
  bool at_start_ = true;
  std::size_t oversized_ = 0;
};

}  // namespace ics::lattice
