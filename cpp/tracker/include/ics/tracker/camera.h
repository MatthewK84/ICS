// camera.h: the tracking camera's frames, and what the tracker needs from a camera (ICS-038, CPP-19).
// The narrow-field tracking camera streams its frames at 100 Hz or more into
// buffers allocated before streaming starts. A frame is a view of one of them,
// lent to the caller until it hands the frame back with release().
#ifndef ICS_TRACKER_CAMERA_H
#define ICS_TRACKER_CAMERA_H

// JSF deviation D-6: ics::Result, ics::Status, ics::Duration and ics::UtcTime come from .hpp headers.
#include <span>
#include <string>

#include <ics/common/error.hpp>
#include <ics/common/units.hpp>
#include <ics/common/universal_types.h>

namespace ics::tracker
{

// The pixel formats the tracker reads: GenICam's Mono8, and Mono16 with
// each pixel in two little-endian bytes. A Mono10 or Mono12 camera sends
// Mono16 with its counts in the low bits.
enum class Pixel_format : uint8
{
  mono8,
  mono16
};

// Where each frame's UTC time comes from.
enum class Stamp_source : uint8
{
  // The camera's own time stamp, PTP-locked to ics-timingd.
  camera,
  // The host's clock when the frame arrived, for a camera without PTP.
  host
};

// A rectangle of the sensor: its top-left pixel and its size.
struct Region
{
  int32 x;
  int32 y;
  int32 width;
  int32 height;
};

// The most buffers a camera streams into.
inline constexpr int32 max_buffers = 32;

// How the camera streams.
struct Camera_settings
{
  // GenICam features to set before the others, as "Name=Value" pairs
  // separated by spaces, such as "PtpEnable=true GevSCPSPacketSize=9000".
  std::string features;
  Pixel_format format;
  // The part of the sensor read out.
  Region region;
  float64 frame_rate_hz;
  float64 exposure_us;
  // Buffers to stream into, 2 to max_buffers.
  int32 buffers;
  // Each buffer's size. 0 asks the camera for its payload, as every
  // configuration does; the tests make it smaller to see frames fail.
  int64 buffer_bytes;
  Stamp_source stamps;
  // Subtracted from each stamp to give UTC: TAI - UTC for a PTP camera on
  // TAI, or the host's receive latency.
  Duration stamp_offset;
};

// A frame, lent until release().
struct Frame
{
  // Counted from 1 since start().
  int64 number;
  // UTC at the frame's exposure.
  UtcTime time;
  int32 width;
  int32 height;
  Pixel_format format;
  // The pixels, row by row from the top-left.
  std::span<const uint8> bytes;
  // The buffer that holds them.
  int32 slot;
};

// The bytes a frame's pixels take: width x height, times 2 for Mono16.
[[nodiscard]] int64 bytes_needed(const int32 width,
                                 const int32 height,
                                 const Pixel_format format);

// The count of pixel (x, y), which must be in the frame and its bytes.
[[nodiscard]] float64 pixel_at(const Frame& frame,
                               const int32 x,
                               const int32 y);

// What the tracker needs from its camera. Aravis_camera drives a GigE Vision
// camera; the tests drive Aravis's fake camera through it.
class Tracking_camera
{
public:
  Tracking_camera() = default;
  Tracking_camera(const Tracking_camera&) = delete;
  Tracking_camera& operator=(const Tracking_camera&) = delete;
  Tracking_camera(Tracking_camera&&) = delete;
  Tracking_camera& operator=(Tracking_camera&&) = delete;
  virtual ~Tracking_camera() = default;

  // Applies the settings, allocates the buffers and starts streaming.
  [[nodiscard]] virtual Status start(const Camera_settings& settings) = 0;
  // The next frame, waiting up to timeout. kEmpty when none came in time.
  [[nodiscard]] virtual Result<Frame> next(const Duration timeout) = 0;
  // Hands a frame's buffer back to stream into.
  [[nodiscard]] virtual Status release(const Frame& frame) = 0;
  // Stops streaming.
  [[nodiscard]] virtual Status stop() = 0;
};

}  // namespace ics::tracker

#endif  // ICS_TRACKER_CAMERA_H
