// Fuzzes the SAPIENT adapter (ICS-024) end to end. The first byte of the
// input picks a chunk size; the rest is a framed stream, fed to StreamReader
// in chunks of that size, and each message it reads goes through one
// adapter, so registrations set the units later detections use. The whole
// input is also parsed as one message and adapted. They must not crash, and
// must keep their promises:
// - the reader stops at the first fault, and reads nothing after it;
// - every record has the object_id as its entity ID, a real geodetic point,
//   finite sigmas that are not negative, a finite velocity, and either the
//   message's timestamp or the receipt time.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/frames/egm96.hpp"
#include "ics/frames/enu.hpp"
#include "ics/frames/geodetic.hpp"
#include "ics/sapient/adapter.hpp"
#include "ics/sapient/stream.hpp"

namespace {

// 2026-10-04T12:00:01Z.
constexpr std::int64_t kReceivedNs = 1'791'115'201'000'000'000;
// 2200-12-31T23:59:59Z.
constexpr std::int64_t kLastSecond = 7'289'654'399;
constexpr int kGridWidth = 8;
constexpr int kGridHeight = 5;
constexpr std::uint16_t kGridSample = 1000;

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

// Whether the record's time is the message's timestamp, which must then be
// one the adapter takes, from 1970 to 2200.
bool is_sent_time(const ics::v1::PliRecord& record, const ics::sapient::Message& message) {
  const google::protobuf::Timestamp& sent = message.timestamp();
  require(sent.seconds() >= 0 && sent.seconds() <= kLastSecond && sent.nanos() >= 0);
  return record.valid_utc_ns() == (sent.seconds() * 1'000'000'000) + sent.nanos();
}

bool finite_sigma(const bool has, const double sigma) { return !has || (std::isfinite(sigma) && sigma >= 0.0); }

void require_sound(const ics::v1::PliRecord& record, const ics::sapient::Message& message) {
  const ics::v1::GeodeticPoint& point = record.position();
  require(ics::frames::Geodetic::make(ics::Degrees(point.latitude_deg()), ics::Degrees(point.longitude_deg()),
                                      ics::Meters(point.height_ellipsoid_m()))
              .has_value());
  require(!record.entity_id().empty() && record.entity_id() == message.detection_report().object_id());
  require(finite_sigma(record.has_horizontal_sigma_m(), record.horizontal_sigma_m()));
  require(finite_sigma(record.has_vertical_sigma_m(), record.vertical_sigma_m()));
  const ics::v1::EnuVector& velocity = record.velocity_enu_mps();
  require(std::isfinite(velocity.east()) && std::isfinite(velocity.north()) && std::isfinite(velocity.up()));
  const bool receipt = record.time_basis() == ics::v1::PLI_TIME_BASIS_RECEIPT && record.valid_utc_ns() == kReceivedNs;
  require(receipt || (record.time_basis() == ics::v1::PLI_TIME_BASIS_VEHICLE_GNSS && is_sent_time(record, message)));
}

void adapt(ics::sapient::Adapter& adapter, const ics::sapient::Message& message) {
  const std::optional<ics::v1::PliRecord> record = adapter.receive(message, ics::utc_from_ns(kReceivedNs));
  if (record) {
    require(message.has_detection_report());
    require_sound(*record, message);
  }
}

ics::sapient::Adapter make_adapter() {
  const ics::frames::EnuFrame range(
      ics::frames::Geodetic::make(ics::Degrees(40.0), ics::Degrees(-100.0), ics::Meters(700.0)).value());
  return ics::sapient::Adapter::make({.max_nodes = 4}, geoid(), range).value();
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  const std::span<const std::byte> input = std::as_bytes(std::span(data, size));
  ics::sapient::Adapter adapter = make_adapter();
  ics::sapient::Message whole;
  if (whole.ParseFromArray(input.data(), static_cast<int>(input.size()))) {
    adapt(adapter, whole);
  }
  if (input.empty()) {
    return 0;
  }
  const std::size_t chunk = std::to_integer<std::size_t>(input.front()) + 1;
  const std::span<const std::byte> stream = input.subspan(1);
  ics::sapient::StreamReader reader;
  for (std::size_t at = 0; at < stream.size(); at += chunk) {
    const bool was_broken = reader.broken();
    const std::vector<ics::sapient::Message> messages =
        reader.feed(stream.subspan(at, std::min(chunk, stream.size() - at)));
    require(!was_broken || messages.empty());
    for (const ics::sapient::Message& message : messages) {
      adapt(adapter, message);
    }
  }
  return 0;
}
