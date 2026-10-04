// Fuzzes the Lattice adapter (ICS-023) end to end. The first byte of the
// input picks a chunk size; the rest is an event stream, fed to SseReader in
// chunks of that size. Each event's data is decoded and goes through the
// adapter, and the whole input is also decoded as one event's data. They must
// not crash, and must keep their promises:
// - every entity event has an entity ID, and its times fall in 1970 to 2200;
// - every record's position is a real geodetic point, its sigmas finite and
//   not negative, its velocity finite, and its time the source's or the
//   receipt time.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string_view>

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/frames/enu.hpp"
#include "ics/frames/geodetic.hpp"
#include "ics/lattice/adapter.hpp"
#include "ics/lattice/event.hpp"
#include "ics/lattice/sse.hpp"

namespace {

// 2026-10-04T12:00:01Z.
constexpr std::int64_t kReceivedNs = 1'791'115'201'000'000'000;
constexpr std::int64_t kLastNs = 7'289'654'399'999'999'999;
constexpr std::size_t kMaxEventBytes = 1U << 16U;

void require(const bool condition) {
  if (!condition) {
    std::abort();
  }
}

void require_in_range(const std::optional<ics::UtcTime>& time) {
  require(!time || (ics::to_utc_ns(*time) >= 0 && ics::to_utc_ns(*time) <= kLastNs));
}

void require_sound(const ics::v1::PliRecord& record, const ics::lattice::Event& event) {
  const ics::v1::GeodeticPoint& point = record.position();
  require(ics::frames::Geodetic::make(ics::Degrees(point.latitude_deg()), ics::Degrees(point.longitude_deg()),
                                      ics::Meters(point.height_ellipsoid_m()))
              .has_value());
  require(record.entity_id() == event.entity.entity_id);
  require(!record.has_horizontal_sigma_m() ||
          (std::isfinite(record.horizontal_sigma_m()) && record.horizontal_sigma_m() >= 0.0));
  require(!record.has_vertical_sigma_m() ||
          (std::isfinite(record.vertical_sigma_m()) && record.vertical_sigma_m() >= 0.0));
  const ics::v1::EnuVector& velocity = record.velocity_enu_mps();
  require(std::isfinite(velocity.east()) && std::isfinite(velocity.north()) && std::isfinite(velocity.up()));
  const bool receipt = record.time_basis() == ics::v1::PLI_TIME_BASIS_RECEIPT && record.valid_utc_ns() == kReceivedNs;
  const bool source = record.time_basis() == ics::v1::PLI_TIME_BASIS_VEHICLE_GNSS &&
                      event.entity.source_update_time.has_value() &&
                      record.valid_utc_ns() == ics::to_utc_ns(*event.entity.source_update_time);
  require(receipt || source);
}

void adapt(const std::string_view data) {
  const ics::Result<std::optional<ics::lattice::Event>> decoded = ics::lattice::decode_event(data);
  if (!decoded || !decoded->has_value()) {
    return;
  }
  const ics::lattice::Event& event = **decoded;
  require(!event.entity.entity_id.empty());
  require_in_range(event.time);
  require_in_range(event.entity.source_update_time);
  const ics::frames::EnuFrame range(
      ics::frames::Geodetic::make(ics::Degrees(40.0), ics::Degrees(-100.0), ics::Meters(700.0)).value());
  const ics::lattice::Adapter adapter = ics::lattice::Adapter::make({}, range).value();
  const std::optional<ics::v1::PliRecord> record = adapter.record(event, ics::utc_from_ns(kReceivedNs));
  if (record) {
    require_sound(*record, event);
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  const std::string_view input(reinterpret_cast<const char*>(data), size);
  adapt(input);
  if (input.empty()) {
    return 0;
  }
  const std::size_t chunk = static_cast<std::size_t>(static_cast<unsigned char>(input.front())) + 1;
  const std::string_view stream = input.substr(1);
  ics::lattice::SseReader reader(kMaxEventBytes);
  for (std::size_t at = 0; at < stream.size(); at += chunk) {
    for (const ics::lattice::SseEvent& event : reader.feed(stream.substr(at, chunk))) {
      adapt(event.data);
    }
  }
  return 0;
}
