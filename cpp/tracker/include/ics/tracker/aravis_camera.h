// aravis_camera.h: the tracking camera through Aravis (ICS-038). Aravis drives
// GigE Vision cameras, and has a fake camera of its own that the tests use.
// Aravis and GLib are C libraries; their types stay in aravis_camera.cpp, the
// one file under JSF deviation D-8.
#ifndef ICS_TRACKER_ARAVIS_CAMERA_H
#define ICS_TRACKER_ARAVIS_CAMERA_H

// JSF deviation D-6: ics::Result, ics::Status, ics::Duration and ics::UtcTime come from .hpp headers.
#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>

#include <ics/common/error.hpp>
#include <ics/common/units.hpp>
#include <ics/common/universal_types.h>
#include <ics/tracker/camera.h>

namespace ics::tracker
{

// The GenICam pixel format codes (PFNC) of Mono8 and Mono16.
inline constexpr uint32 mono8_code = 0x01080001U;
inline constexpr uint32 mono16_code = 0x01100007U;

// A buffer as Aravis hands it back, before the tracker trusts it.
struct Buffer_view
{
  // Aravis's buffer status: 0 is success.
  int32 status;
  // Where Aravis says the image starts, and its size.
  const uint8* data;
  std::size_t size;
  int32 width;
  int32 height;
  uint32 pixel_format;
  // The camera's and the host's time stamps, in nanoseconds.
  uint64 camera_ns;
  uint64 host_ns;
};

// The frame a buffer holds: kUnavailable for a buffer Aravis did not fill,
// kMalformed for one whose image is not in storage from its start, is too
// small or is in another pixel format, and kUnconstrained for one without
// the time stamp the settings take.
[[nodiscard]] Result<Frame> frame_of(const Buffer_view& view,
                                     const std::span<const uint8> storage,
                                     const int32 slot,
                                     const int64 number,
                                     const Camera_settings& settings);

// The settings a camera can stream with: kInvalidArgument otherwise.
[[nodiscard]] Status valid(const Camera_settings& settings);

// A camera driven by Aravis.
class Aravis_camera final : public Tracking_camera
{
public:
  // Opens the camera with Aravis's device ID, such as a GigE Vision camera's
  // "<vendor>-<serial>". fake enables Aravis's fake camera, whose ID is
  // "Fake_1". kInvalidArgument for an empty ID; kUnavailable when no camera
  // with the ID answers.
  [[nodiscard]] static Result<std::unique_ptr<Aravis_camera>> open(const std::string& device_id, const bool fake);

  Aravis_camera(const Aravis_camera&) = delete;
  Aravis_camera& operator=(const Aravis_camera&) = delete;
  Aravis_camera(Aravis_camera&&) = delete;
  Aravis_camera& operator=(Aravis_camera&&) = delete;
  // Stops streaming, and releases the camera.
  ~Aravis_camera() override;

  // kInvalidArgument for settings that are not valid() or a camera already
  // streaming; kUnavailable when the camera refuses a setting.
  [[nodiscard]] Status start(const Camera_settings& settings) override;
  // kInvalidArgument before start(); as frame_of() for a frame Aravis did
  // not fill, whose buffer goes straight back to stream into.
  [[nodiscard]] Result<Frame> next(const Duration timeout) override;
  // kInvalidArgument for a frame not from this camera's stream.
  [[nodiscard]] Status release(const Frame& frame) override;
  // Stops streaming; kUnavailable when the camera refuses.
  [[nodiscard]] Status stop() override;

  // Frames Aravis handed back unfilled since start().
  [[nodiscard]] int64 dropped() const;
  // Aravis's message for the last setting the camera refused, if any.
  [[nodiscard]] std::string_view last_error() const;

private:
  struct State;
  explicit Aravis_camera(std::unique_ptr<State> state);
  // Allocates buffers of bytes each and hands them to the stream.
  void allocate(const int64 bytes, const int32 buffers);

  std::unique_ptr<State> state_;
};

}  // namespace ics::tracker

#endif  // ICS_TRACKER_ARAVIS_CAMERA_H
