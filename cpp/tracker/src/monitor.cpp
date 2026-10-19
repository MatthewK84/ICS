// monitor.cpp: ics-tracker-monitor, the operational test of the tracking camera (ICS-038).
// JSF deviation D-6: ics::config and ics::common's Result and Duration come from .hpp headers.
#include <charconv>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <optional>
#include <ostream>
#include <span>
#include <string_view>
#include <system_error>

#include <ics/common/error.hpp>
#include <ics/common/units.hpp>
#include <ics/common/universal_types.h>
#include <ics/config/config.hpp>
#include <ics/config/reader.hpp>
#include <ics/tracker/aravis_camera.h>
#include <ics/tracker/camera.h>
#include <ics/tracker/centroid.h>
#include <ics/tracker/config.h>
#include <ics/tracker/geometry.h>
#include <ics/tracker/monitor.h>
#include <ics/tracker/tracker.h>

namespace
{

using ics::float64;
using ics::int32;
using ics::int64;
using ics::tracker::Detection;
using ics::tracker::Frame;

constexpr std::string_view usage = "usage: ics-tracker-monitor FRAMES\n"
                                   "Streams FRAMES frames, 1 to 1000000000, from the camera that\n"
                                   "/etc/ics/ics-tracker.toml's [tracker] table names. It looks for the target\n"
                                   "around the boresight, then around its last detection, and writes each frame\n"
                                   "as a JSON line, then the count of frames the camera dropped.\n";
constexpr float64 microradians_per_radian = 1.0E6;
constexpr int32 digits = 10;
constexpr ics::Duration frame_wait = std::chrono::seconds(1);

// A frame's line: what was found, where, and against what sky.
void write_frame(std::ostream& out,
                 const Frame& frame,
                 const ics::Result<Detection>& found,
                 const ics::tracker::Camera_geometry& camera)
{
  out << std::setprecision(digits) << "{\"frame\":" << frame.number
      << ",\"time_ns\":" << frame.time.time_since_epoch().count() << ",\"found\":" << (found ? "true" : "false");
  if (found)
  {
    const ics::tracker::Tangent offset = ics::tracker::tangent_of_pixel({found->x, found->y}, camera);
    out << ",\"x\":" << found->x << ",\"y\":" << found->y << ",\"xi_urad\":" << (offset.xi * microradians_per_radian)
        << ",\"eta_urad\":" << (offset.eta * microradians_per_radian) << ",\"area\":" << found->area
        << ",\"peak\":" << found->peak << ",\"background\":" << found->background << ",\"sigma\":" << found->sigma;
  }
  out << "}\n";
}

// Searches one frame: around the last detection, or the boresight.
ics::Result<Detection> search(ics::tracker::Centroider& centroider,
                              const Frame& frame,
                              const ics::tracker::Tracker_settings& settings,
                              const std::optional<Detection>& last)
{
  const ics::tracker::Pixel_point centre = last.has_value()
                                             ? ics::tracker::Pixel_point{last->x, last->y}
                                             : ics::tracker::Pixel_point{settings.camera.boresight_x,
                                                                         settings.camera.boresight_y};
  const ics::tracker::Extent size = last.has_value() ? settings.track_window : settings.acquire_window;
  const ics::Result<ics::tracker::Window> window =
    ics::tracker::window_around(centre, size, ics::tracker::Extent{frame.width, frame.height});
  ics::Result<Detection> found = ics::fail(ics::Error::kOutOfRange);
  if (window.has_value())
  {
    found = centroider.find(frame, *window, centre);
  }
  return found;
}

// Streams and searches the frames of a started camera.
void stream(ics::tracker::Aravis_camera& camera,
            const ics::tracker::Tracker_settings& settings,
            const int64 frames,
            std::ostream& out)
{
  ics::tracker::Centroider centroider(settings.centroid);
  std::optional<Detection> last;
  for (int64 count = 0; count < frames; ++count)
  {
    const ics::Result<Frame> frame = camera.next(frame_wait);
    if (frame.has_value())
    {
      const ics::Result<Detection> found = search(centroider, *frame, settings, last);
      write_frame(out, *frame, found, settings.camera);
      last = found.has_value() ? std::optional<Detection>(*found) : std::nullopt;
      static_cast<void>(camera.release(*frame));
    }
    else
    {
      out << "{\"error\":\"" << ics::to_string(frame.error()) << "\"}\n";
    }
  }
  out << "{\"dropped\":" << camera.dropped() << "}\n";
}

// The monitor, once the arguments are known to be a frame count.
int32 run(const std::filesystem::path& config_file,
          const int64 frames,
          std::ostream& out,
          std::ostream& errors)
{
  int32 code = ics::tracker::exit_failed;
  const auto config = ics::config::read_file(config_file, &ics::tracker::read_tracker_config);
  if (config.has_value())
  {
    code = ics::tracker::monitor_camera(*config, frames, out, errors);
  }
  else
  {
    errors << ics::config::format_errors(config_file.string(), config.error());
  }
  return code;
}

// The frame count an argument gives: 1 to max_frames, or 0 for none.
int64 frames_in(const std::string_view argument)
{
  int64 frames = 0;
  const std::from_chars_result parsed = std::from_chars(argument.data(), argument.data() + argument.size(), frames);
  const bool whole = (parsed.ec == std::errc()) && (parsed.ptr == (argument.data() + argument.size()));
  return (whole && (frames >= 1) && (frames <= ics::tracker::max_frames)) ? frames : 0;
}

}  // namespace

namespace ics::tracker
{

int32 monitor_camera(const Tracker_config& config,
                     const int64 frames,
                     std::ostream& out,
                     std::ostream& errors)
{
  int32 code = exit_failed;
  Result<std::unique_ptr<Aravis_camera>> camera = fail(Error::kInvalidArgument);
  if (!(valid(config.camera).has_value() && valid(config.tracker).has_value()))
  {
    errors << "the [tracker] settings do not make a camera and a tracker that can run\n";
  }
  else
  {
    camera = Aravis_camera::open(config.camera_id, config.fake_camera);
    if (!camera.has_value())
    {
      errors << "camera \"" << config.camera_id << "\" did not answer: " << to_string(camera.error()) << "\n";
    }
  }
  if (camera.has_value())
  {
    const Status started = (*camera)->start(config.camera);
    if (started.has_value())
    {
      stream(**camera, config.tracker, frames, out);
      code = exit_monitored;
    }
    else
    {
      errors << "camera \"" << config.camera_id << "\" did not start: " << (*camera)->last_error() << "\n";
    }
  }
  return code;
}

int32 run_monitor(const std::span<const char* const> arguments,
                  const std::filesystem::path& config_file,
                  std::ostream& out,
                  std::ostream& errors)
{
  const int64 count = static_cast<int64>(arguments.size());
  const std::string_view argument = (count == 2) ? std::string_view(arguments[1]) : std::string_view();
  const int64 frames = frames_in(argument);
  int32 code = exit_usage;
  if (frames > 0)
  {
    code = run(config_file, frames, out, errors);
  }
  else if (argument == "--help")
  {
    out << usage;
    code = exit_monitored;
  }
  else
  {
    errors << usage;
  }
  return code;
}

}  // namespace ics::tracker
