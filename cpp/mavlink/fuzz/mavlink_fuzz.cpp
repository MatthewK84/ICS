// Fuzzes the MAVLink adapter (ICS-021) end to end. The input is read as a
// captured Ethernet frame, and as the payload of a UDP datagram; when it holds
// a datagram, that datagram's payload is read too. Each payload's frames go
// through the adapter, which must not crash and must keep its promises:
// - the UDP payload lies within the frame it came from;
// - FrameReader only returns frames of messages ICS reads;
// - no frame yields more than one record, and every record's latitude,
//   longitude and height are a real geodetic point;
// - every event names the system it concerns.
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <span>
#include <string>

#include "ics/capture/datagram.hpp"
#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/frames/egm96.hpp"
#include "ics/frames/enu.hpp"
#include "ics/frames/geodetic.hpp"
#include "ics/mavlink/adapter.hpp"
#include "ics/mavlink/frame.hpp"

namespace {

using ics::mavlink::Output;

// Ethernet, IPv4 and UDP headers at their smallest.
constexpr std::size_t kSmallestHeaders = 14 + 20 + 8;
constexpr int kGridWidth = 8;
constexpr int kGridHeight = 5;
constexpr std::uint16_t kGridSample = 1000;
constexpr std::int64_t kStartNs = 1'790'000'000'000'000'000;

void require(const bool condition) {
  if (!condition) {
    std::abort();
  }
}

// A small grid of constant height: the adapter's use of the geoid is under
// test here, not EGM96 itself (frames/fuzz does that), and the fuzzer must not
// depend on the installed grid.
const ics::frames::Egm96& geoid() {
  static const ics::Result<ics::frames::Egm96> grid = [] {
    std::string file = "P5\n# Offset -100\n# Scale 0.1\n8 5\n65535\n";
    for (int i = 0; i < kGridWidth * kGridHeight; ++i) {
      file.push_back(static_cast<char>(kGridSample >> 8U));
      file.push_back(static_cast<char>(kGridSample & 0xFFU));
    }
    return ics::frames::Egm96::parse(file);
  }();
  require(grid.has_value());
  return *grid;
}

void require_sound(const Output& out, const std::size_t frames) {
  require(out.positions.size() <= frames);
  for (const ics::mavlink::Position& position : out.positions) {
    const ics::v1::GeodeticPoint& point = position.record.position();
    require(ics::frames::Geodetic::make(ics::Degrees(point.latitude_deg()), ics::Degrees(point.longitude_deg()),
                                        ics::Meters(point.height_ellipsoid_m()))
                .has_value());
  }
  for (const ics::v1::PliEvent& event : out.events) {
    require(!event.entity_id().empty());
  }
}

void adapt(const std::span<const std::byte> payload) {
  const ics::frames::EnuFrame range(
      ics::frames::Geodetic::make(ics::Degrees(40.0), ics::Degrees(-100.0), ics::Meters(700.0)).value());
  ics::mavlink::Adapter adapter({}, geoid(), range);
  Output out;
  ics::UtcTime now = ics::utc_from_ns(kStartNs);
  std::size_t frames = 0;
  ics::mavlink::FrameReader reader(payload);
  for (std::optional<ics::mavlink::Frame> frame = reader.next(); frame; frame = reader.next()) {
    require(ics::mavlink::crc_extra(frame->message_id).has_value());
    adapter.receive(*frame, now, out);
    now += std::chrono::milliseconds(100);
    ++frames;
  }
  adapter.tick(now + std::chrono::hours(1), out);
  require_sound(out, frames);
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  const std::span<const std::byte> input = std::as_bytes(std::span<const std::uint8_t>(data, size));
  const std::optional<ics::capture::Datagram> datagram = ics::capture::udp_datagram(input);
  if (datagram) {
    require(datagram->payload.size() + kSmallestHeaders <= input.size());
    adapt(datagram->payload);
  }
  adapt(input);
  return 0;
}
