// Fuzzes the CoT adapter (ICS-022) end to end. The input is read as a
// captured Ethernet frame, and as the payload of a UDP datagram; when it holds
// a datagram, that datagram's payload is read too. Each payload's events go
// through the adapter, which must not crash and must keep its promises:
// - a malformed payload gives no events;
// - every event has a uid, a type and a point of finite numbers, with the
//   latitude and longitude in range, and any track is a usable one;
// - no event yields more than one record, and every record's position is a
//   real geodetic point, its errors and velocity finite, and its time the
//   event's or the receipt time.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <span>

#include "ics/capture/datagram.hpp"
#include "ics/common/units.hpp"
#include "ics/cot/adapter.hpp"
#include "ics/cot/event.hpp"
#include "ics/cot/uncertainty.hpp"
#include "ics/frames/enu.hpp"
#include "ics/frames/geodetic.hpp"

namespace {

// Ethernet, IPv4 and UDP headers at their smallest.
constexpr std::size_t kSmallestHeaders = 14 + 20 + 8;
// 2026-10-04T12:00:01Z.
constexpr std::int64_t kReceivedNs = 1'791'115'201'000'000'000;
constexpr double kMaxLatitude = 90.0;
constexpr double kMaxLongitude = 180.0;
constexpr double kFullCircle = 360.0;

void require(const bool condition) {
  if (!condition) {
    std::abort();
  }
}

void require_sound(const ics::cot::Event& event) {
  const ics::cot::Point& point = event.point;
  require(!event.uid.empty() && !event.type.empty());
  require(std::isfinite(point.hae_m) && std::isfinite(point.ce_m) && std::isfinite(point.le_m));
  require(std::abs(point.latitude_deg) <= kMaxLatitude && std::abs(point.longitude_deg) <= kMaxLongitude);
  if (event.track) {
    require(event.track->course_deg >= 0.0 && event.track->course_deg < kFullCircle);
    require(event.track->speed_mps >= 0.0 && event.track->speed_mps < ics::cot::kUnknown);
  }
}

void require_sound(const ics::v1::PliRecord& record, const ics::cot::Event& event) {
  const ics::v1::GeodeticPoint& point = record.position();
  require(ics::frames::Geodetic::make(ics::Degrees(point.latitude_deg()), ics::Degrees(point.longitude_deg()),
                                      ics::Meters(point.height_ellipsoid_m()))
              .has_value());
  require(record.entity_id() == event.uid);
  require(!record.has_horizontal_sigma_m() || std::isfinite(record.horizontal_sigma_m()));
  require(!record.has_vertical_sigma_m() || std::isfinite(record.vertical_sigma_m()));
  const ics::v1::EnuVector& velocity = record.velocity_enu_mps();
  require(std::isfinite(velocity.east()) && std::isfinite(velocity.north()) && std::isfinite(velocity.up()));
  const bool receipt = record.time_basis() == ics::v1::PLI_TIME_BASIS_RECEIPT && record.valid_utc_ns() == kReceivedNs;
  const bool own = record.time_basis() == ics::v1::PLI_TIME_BASIS_VEHICLE_GNSS && event.time.has_value() &&
                   record.valid_utc_ns() == ics::to_utc_ns(*event.time);
  require(receipt || own);
}

void adapt(const std::span<const std::byte> payload) {
  const ics::frames::EnuFrame range(
      ics::frames::Geodetic::make(ics::Degrees(40.0), ics::Degrees(-100.0), ics::Meters(700.0)).value());
  const ics::cot::Adapter adapter = ics::cot::Adapter::make({}, range).value();
  const ics::cot::Read read = ics::cot::read_events(payload);
  require(read.counts.malformed == 0 || read.events.empty());
  for (const ics::cot::Event& event : read.events) {
    require_sound(event);
    const std::optional<ics::v1::PliRecord> record = adapter.record(event, ics::utc_from_ns(kReceivedNs));
    if (record) {
      require_sound(*record, event);
    }
  }
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
