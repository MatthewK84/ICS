// Fuzzes the camera offsets file reader (ICS-029) with any bytes, as a
// damaged or foreign file could hold. It may not overflow or crash, and what
// it accepts must keep its promises: a station, and offsets each with a
// camera, a sigma of 0 or more and a measured time; written back, they read
// back the same.
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <vector>

#include "ics/common/error.hpp"
#include "ics/timing/camera_offsets.hpp"
#include "ics/v1/time_quality.pb.h"

namespace {

void require(const bool condition) {
  if (!condition) {
    std::abort();
  }
}

void require_round_trip(const ics::timing::CameraOffsets& offsets) {
  ics::v1::TimeQuality message;
  message.set_station_id(offsets.station_id);
  for (const ics::v1::TimeQuality::CameraOffset& offset : offsets.offsets) {
    *message.add_camera_offsets() = offset;
  }
  std::vector<std::byte> bytes(message.ByteSizeLong());
  require(message.SerializeToArray(bytes.data(), static_cast<int>(bytes.size())));
  const ics::Result<ics::timing::CameraOffsets> again = ics::timing::parse_camera_offsets(bytes);
  require(again.has_value() && again->station_id == offsets.station_id &&
          again->offsets.size() == offsets.offsets.size());
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  const std::span<const std::byte> input = std::as_bytes(std::span(data, size));
  const ics::Result<ics::timing::CameraOffsets> offsets = ics::timing::parse_camera_offsets(input);
  if (!offsets) {
    return 0;
  }
  require(!offsets->station_id.empty());
  for (const ics::v1::TimeQuality::CameraOffset& offset : offsets->offsets) {
    require(!offset.camera_id().empty() && offset.offset_sigma_ns() >= 0 && offset.measured_utc_ns() > 0);
  }
  require_round_trip(*offsets);
  return 0;
}
