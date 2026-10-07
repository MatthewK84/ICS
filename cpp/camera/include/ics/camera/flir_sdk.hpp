#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "ics/camera/irig.hpp"
#include "ics/camera/segment_camera.hpp"
#include "ics/common/error.hpp"
#include "ics/common/units.hpp"

namespace ics::camera {

// A frame as the FLIR X6980-HS hands it over: its number counted from its
// segment's trigger frame (0), the IRIG-B time stamp its decoder latched,
// its integration time, and its pixels: width x height little-endian 16-bit
// words, row by row, of which the sensor fills 14 bits.
struct FlirFrame {
  std::int32_t number = 0;
  IrigStamp stamp{};
  Duration integration{};
  std::vector<std::byte> pixels;
};

// A segment once its trigger came and its post-trigger frames are recorded:
// the trigger's IRIG-B time stamp, and the frames the segment holds.
struct FlirTrigger {
  std::uint32_t segment = 0;
  IrigStamp stamp{};
  FrameRange recorded{};
};

// What ICS needs from the X6980's SDK, frame by frame. An emulated camera
// implements it now, and a binding of the SDK will once ICS has it (ICS-028).
class FlirSdk {
 public:
  FlirSdk() = default;
  FlirSdk(const FlirSdk&) = delete;
  FlirSdk& operator=(const FlirSdk&) = delete;
  FlirSdk(FlirSdk&&) = delete;
  FlirSdk& operator=(FlirSdk&&) = delete;
  virtual ~FlirSdk() = default;

  // Applies the sensor window, frame rate and integration time, and returns
  // the settings as the camera applied them.
  [[nodiscard]] virtual Result<CameraSettings> configure(const CameraSettings& settings) = 0;
  // Splits the camera's memory into segments and starts recording into the
  // first.
  [[nodiscard]] virtual Status arm(std::uint32_t segments) = 0;
  // Triggers the segment recording now, and returns once its post-trigger
  // frames are recorded; the camera then records into the next segment.
  [[nodiscard]] virtual Result<FlirTrigger> trigger() = 0;
  // Reads frames of a recorded segment, in order.
  [[nodiscard]] virtual Result<std::vector<FlirFrame>> read(std::uint32_t segment, const FrameRange& frames) = 0;
};

}  // namespace ics::camera
