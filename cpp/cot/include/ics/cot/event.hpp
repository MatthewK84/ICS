#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "ics/common/units.hpp"

namespace ics::cot {

// Cursor on Target events (ICS-022): the <event> elements in the payload of
// one UDP datagram, read with pugixml. A datagram usually holds one event; a
// payload of several top-level events is read as a fragment. Only the parts
// the adapter uses are kept.
//
// pugixml reads no DTD and expands no entities beyond XML's five and
// character references, so an untrusted payload cannot reach files or the
// network, or grow without bound.

// <point>: degrees, and metres above the WGS84 ellipsoid. hae, ce and le may
// be CoT's unknown value, 9999999 (uncertainty.hpp).
struct Point {
  double latitude_deg = 0.0;
  double longitude_deg = 0.0;
  double hae_m = 0.0;
  double ce_m = 0.0;
  double le_m = 0.0;
};

// <detail><track>: the course over the ground in degrees from true north, and
// the speed in metres per second.
struct Track {
  double course_deg = 0.0;
  double speed_mps = 0.0;
};

struct Event {
  std::string uid;
  std::string type;
  std::string how;
  // The event's time attribute: nothing when it is missing or malformed.
  std::optional<UtcTime> time;
  Point point;
  // Nothing when there is no track, or its course or speed is missing,
  // malformed or unknown.
  std::optional<Track> track;
};

// What a payload held besides the events read from it.
struct ReadCounts {
  // Payloads that were not well-formed XML; none of their events are read.
  std::size_t malformed = 0;
  // Top-level elements other than <event>.
  std::size_t not_events = 0;
  // Events without a uid or type, or without a <point> of five finite
  // numbers with the latitude and longitude in range.
  std::size_t invalid = 0;
};

struct Read {
  std::vector<Event> events;
  ReadCounts counts;
};

// The events in a datagram's payload.
[[nodiscard]] Read read_events(std::span<const std::byte> payload);

}  // namespace ics::cot
