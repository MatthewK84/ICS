// aravis_camera.cpp: the tracking camera through Aravis (ICS-038).
// JSF deviation D-8: Aravis and GLib's C API, whose calls report errors through
// a GError** out-parameter (AV 169). It stays in this file, and no Aravis or
// GLib macro is used.
// JSF deviation D-6: ics::Result, ics::Status and ics::Duration come from .hpp headers.
#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <arv.h>
#include <glib.h>

#include <ics/common/error.hpp>
#include <ics/common/units.hpp>
#include <ics/common/universal_types.h>
#include <ics/tracker/aravis_camera.h>
#include <ics/tracker/camera.h>

namespace
{

using ics::Error;
using ics::fail;
using ics::float64;
using ics::int32;
using ics::int64;
using ics::uint32;
using ics::uint64;
using ics::uint8;
using ics::tracker::Camera_settings;
using ics::tracker::Frame;
using ics::tracker::Pixel_format;

// Aravis's status for a buffer it filled.
constexpr int32 success_status = 0;
// The most frames a second and the longest exposure the tracker asks for.
constexpr float64 max_frame_rate_hz = 10000.0;
constexpr float64 max_exposure_us = 1.0E6;
// The largest buffer the tracker allocates.
constexpr int64 max_buffer_bytes = 256LL * 1024LL * 1024LL;
constexpr int64 nanoseconds_per_microsecond = 1000;

// Releases a GObject.
template <typename Object>
struct Unref
{
  void operator()(Object* object) const
  {
    g_object_unref(object);
  }
};

using Camera_pointer = std::unique_ptr<ArvCamera, Unref<ArvCamera>>;
using Stream_pointer = std::unique_ptr<ArvStream, Unref<ArvStream>>;

// The pixel format a GenICam code names, if the tracker reads it.
bool format_of(const uint32 code, Pixel_format& format)
{
  const bool mono8 = code == ics::tracker::mono8_code;
  const bool known = mono8 || (code == ics::tracker::mono16_code);
  format = mono8 ? Pixel_format::mono8 : Pixel_format::mono16;
  return known;
}

// The GenICam code of a pixel format.
uint32 code_of(const Pixel_format format)
{
  return (format == Pixel_format::mono8) ? ics::tracker::mono8_code : ics::tracker::mono16_code;
}

// A stamp in nanoseconds as UTC, less the offset; false for no stamp.
bool time_of(const uint64 stamp_ns,
             const ics::Duration offset,
             ics::UtcTime& time)
{
  const bool stamped = (stamp_ns > 0U) && (stamp_ns <= static_cast<uint64>(std::numeric_limits<int64>::max()));
  time = (ics::UtcTime{} + ics::Duration(static_cast<int64>(stamped ? stamp_ns : 0U))) - offset;
  return stamped;
}

// Takes a call's error: kUnavailable with its message kept, or success.
ics::Status refused(GError*& error, std::string& message)
{
  ics::Status status{};
  if (error != nullptr)
  {
    message = error->message;
    g_clear_error(&error);
    status = fail(Error::kUnavailable);
  }
  return status;
}

// Applies the settings to the camera, each only while the ones before it
// took, leaving the first refusal in error.
void configure(ArvCamera* camera,
               const Camera_settings& settings,
               GError*& error)
{
  const ics::tracker::Region& region = settings.region;
  arv_device_set_features_from_string(arv_camera_get_device(camera), settings.features.c_str(), &error);
  if (error == nullptr)
  {
    arv_camera_set_pixel_format(camera, code_of(settings.format), &error);
  }
  if (error == nullptr)
  {
    arv_camera_set_region(camera, region.x, region.y, region.width, region.height, &error);
  }
  if (error == nullptr)
  {
    arv_camera_set_acquisition_mode(camera, ARV_ACQUISITION_MODE_CONTINUOUS, &error);
  }
  if (error == nullptr)
  {
    arv_camera_set_frame_rate(camera, settings.frame_rate_hz, &error);
  }
  if (error == nullptr)
  {
    arv_camera_set_exposure_time(camera, settings.exposure_us, &error);
  }
}

}  // namespace

namespace ics::tracker
{

// The camera, its stream, and the memory it streams into. The stream is
// released before the memory its buffers point into.
struct Aravis_camera::State
{
  std::array<std::vector<uint8>, static_cast<std::size_t>(max_buffers)> storage;
  std::array<ArvBuffer*, static_cast<std::size_t>(max_buffers)> buffers{};
  Camera_pointer camera;
  Stream_pointer stream;
  Camera_settings settings{};
  int32 count = 0;
  bool streaming = false;
  int64 frames = 0;
  int64 dropped = 0;
  std::string last_error;
};

Result<Frame> frame_of(const Buffer_view& view,
                       const std::span<const uint8> storage,
                       const int32 slot,
                       const int64 number,
                       const Camera_settings& settings)
{
  Result<Frame> frame = fail(Error::kUnavailable);
  if (view.status == success_status)
  {
    Pixel_format format = Pixel_format::mono8;
    const bool known = format_of(view.pixel_format, format);
    const int64 needed = bytes_needed(view.width, view.height, format);
    const bool sized = (needed <= static_cast<int64>(view.size)) && (view.size <= storage.size());
    const bool shaped = known && (view.width > 0) && (view.height > 0) && (view.data == storage.data()) && sized;
    frame = fail(Error::kMalformed);
    if (shaped)
    {
      UtcTime time{};
      const uint64 stamp = (settings.stamps == Stamp_source::camera) ? view.camera_ns : view.host_ns;
      frame = fail(Error::kUnconstrained);
      if (time_of(stamp, settings.stamp_offset, time))
      {
        frame = Frame{number, time, view.width, view.height, format, storage.first(view.size), slot};
      }
    }
  }
  return frame;
}

Status valid(const Camera_settings& settings)
{
  const Region& region = settings.region;
  const bool placed = (region.x >= 0) && (region.y >= 0) && (region.width > 0) && (region.height > 0);
  const bool timed = (settings.frame_rate_hz > 0.0) && (settings.frame_rate_hz <= max_frame_rate_hz);
  const bool exposed = (settings.exposure_us > 0.0) && (settings.exposure_us <= max_exposure_us);
  const bool buffered = (settings.buffers >= 2) && (settings.buffers <= max_buffers);
  const bool sized = (settings.buffer_bytes >= 0) && (settings.buffer_bytes <= max_buffer_bytes);
  Status status{};
  if (!(placed && timed && exposed && buffered && sized))
  {
    status = fail(Error::kInvalidArgument);
  }
  return status;
}

Aravis_camera::Aravis_camera(std::unique_ptr<State> state) : state_(std::move(state))
{
}

Result<std::unique_ptr<Aravis_camera>> Aravis_camera::open(const std::string& device_id, const bool fake)
{
  Result<std::unique_ptr<Aravis_camera>> opened = fail(Error::kInvalidArgument);
  if (!device_id.empty())
  {
    if (fake)
    {
      arv_enable_interface("Fake");
    }
    arv_update_device_list();
    GError* error = nullptr;
    Camera_pointer camera(arv_camera_new(device_id.c_str(), &error));
    g_clear_error(&error);
    opened = fail(Error::kUnavailable);
    if (camera != nullptr)
    {
      std::unique_ptr<State> state = std::make_unique<State>();
      state->camera = std::move(camera);
      opened = std::unique_ptr<Aravis_camera>(new Aravis_camera(std::move(state)));
    }
  }
  return opened;
}

Aravis_camera::~Aravis_camera()
{
  static_cast<void>(stop());
}

Status Aravis_camera::start(const Camera_settings& settings)
{
  Status status = valid(settings);
  if (status && state_->streaming)
  {
    status = fail(Error::kInvalidArgument);
  }
  if (status)
  {
    ArvCamera* camera = state_->camera.get();
    GError* error = nullptr;
    configure(camera, settings, error);
    if (error == nullptr)
    {
      state_->stream = Stream_pointer(arv_camera_create_stream(camera, nullptr, nullptr, &error));
    }
    if (error == nullptr)
    {
      const int64 payload = static_cast<int64>(arv_camera_get_payload(camera, &error));
      allocate((settings.buffer_bytes == 0) ? payload : settings.buffer_bytes, settings.buffers);
      arv_camera_start_acquisition(camera, &error);
    }
    status = refused(error, state_->last_error);
    state_->settings = settings;
    state_->streaming = static_cast<bool>(status);
    state_->frames = 0;
    state_->dropped = 0;
  }
  return status;
}

void Aravis_camera::allocate(const int64 bytes, const int32 buffers)
{
  // A payload the camera reports outside 1 byte to max_buffer_bytes is held to
  // that range: each frame then comes back the wrong size and is dropped.
  const std::size_t length = static_cast<std::size_t>(std::clamp(bytes, int64{1}, max_buffer_bytes));
  state_->count = buffers;
  for (int32 slot = 0; slot < buffers; ++slot)
  {
    std::vector<uint8>& memory = state_->storage[static_cast<std::size_t>(slot)];
    memory.assign(length, 0U);
    ArvBuffer* buffer = arv_buffer_new(memory.size(), memory.data());
    state_->buffers[static_cast<std::size_t>(slot)] = buffer;
    arv_stream_push_buffer(state_->stream.get(), buffer);
  }
}

Result<Frame> Aravis_camera::next(const Duration timeout)
{
  Result<Frame> frame = fail(Error::kInvalidArgument);
  if (state_->streaming)
  {
    const int64 microseconds = timeout.count() / nanoseconds_per_microsecond;
    ArvBuffer* buffer = arv_stream_timeout_pop_buffer(state_->stream.get(), static_cast<uint64>(microseconds));
    frame = fail(Error::kEmpty);
    if (buffer != nullptr)
    {
      int32 slot = 0;
      for (int32 index = 0; index < state_->count; ++index)
      {
        slot = (state_->buffers[static_cast<std::size_t>(index)] == buffer) ? index : slot;
      }
      std::size_t size = 0U;
      const void* image = arv_buffer_get_status(buffer) == ARV_BUFFER_STATUS_SUCCESS
                              ? arv_buffer_get_image_data(buffer, &size)
                              : nullptr;
      const std::vector<uint8>& memory = state_->storage[static_cast<std::size_t>(slot)];
      const bool filled = image != nullptr;
      const Buffer_view view{static_cast<int32>(arv_buffer_get_status(buffer)),
                             (image == memory.data()) ? memory.data() : nullptr,
                             size,
                             filled ? arv_buffer_get_image_width(buffer) : 0,
                             filled ? arv_buffer_get_image_height(buffer) : 0,
                             filled ? arv_buffer_get_image_pixel_format(buffer) : 0U,
                             arv_buffer_get_timestamp(buffer),
                             arv_buffer_get_system_timestamp(buffer)};
      state_->frames += 1;
      frame = frame_of(view, memory, slot, state_->frames, state_->settings);
      if (!frame)
      {
        state_->dropped += 1;
        arv_stream_push_buffer(state_->stream.get(), buffer);
      }
    }
  }
  return frame;
}

Status Aravis_camera::release(const Frame& frame)
{
  Status status = fail(Error::kInvalidArgument);
  const bool ours = (frame.slot >= 0) && (frame.slot < state_->count);
  if (state_->streaming && ours)
  {
    arv_stream_push_buffer(state_->stream.get(), state_->buffers[static_cast<std::size_t>(frame.slot)]);
    status = Status{};
  }
  return status;
}

Status Aravis_camera::stop()
{
  Status status{};
  if (state_->streaming)
  {
    GError* error = nullptr;
    arv_camera_stop_acquisition(state_->camera.get(), &error);
    status = refused(error, state_->last_error);
    state_->stream.reset();
    state_->streaming = false;
  }
  return status;
}

int64 Aravis_camera::dropped() const
{
  return state_->dropped;
}

std::string_view Aravis_camera::last_error() const
{
  return state_->last_error;
}

}  // namespace ics::tracker
