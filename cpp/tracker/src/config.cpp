// config.cpp: the [tracker] table's schema (ICS-038).
// JSF deviation D-6: ics::config and ics::common's units come from .hpp headers.
#include <array>
#include <chrono>
#include <cstddef>
#include <string>

#include <ics/common/units.hpp>
#include <ics/common/universal_types.h>
#include <ics/config/reader.hpp>
#include <ics/tracker/camera.h>
#include <ics/tracker/centroid.h>
#include <ics/tracker/config.h>
#include <ics/tracker/estimator.h>
#include <ics/tracker/geometry.h>
#include <ics/tracker/tracker.h>

namespace
{

using ics::float64;
using ics::int32;
using ics::int64;
using ics::tracker::Polarity;

constexpr int64 max_pixels = 65535;
constexpr float64 max_count = 65535.0;
constexpr float64 min_frame_rate_hz = 0.1;
constexpr float64 max_frame_rate_hz = 10000.0;
constexpr float64 max_exposure_us = 1.0E6;
constexpr float64 min_plate_scale_urad = 0.01;
constexpr float64 max_plate_scale_urad = 10000.0;
constexpr float64 radians_per_microradian = 1.0E-6;
constexpr float64 full_circle_deg = 360.0;
constexpr float64 max_k_sigma = 100.0;
constexpr float64 min_gate_px = 0.1;
constexpr float64 max_beta = 4.0;
constexpr float64 max_gain = 100.0;
constexpr int64 max_confirm_frames = 100;
constexpr std::size_t max_features = 64U;
constexpr ics::Duration max_stamp_offset = std::chrono::seconds(100);
constexpr ics::Duration max_coast = std::chrono::seconds(10);
constexpr ics::Duration max_latency = std::chrono::seconds(1);

constexpr std::array<ics::config::Choice<ics::tracker::Pixel_format>, 2> formats{
  {{"mono8", ics::tracker::Pixel_format::mono8}, {"mono16", ics::tracker::Pixel_format::mono16}}};
constexpr std::array<ics::config::Choice<ics::tracker::Stamp_source>, 2> stamps{
  {{"camera", ics::tracker::Stamp_source::camera}, {"host", ics::tracker::Stamp_source::host}}};
constexpr std::array<ics::config::Choice<Polarity>, 3> polarities{
  {{"dark", Polarity::dark}, {"bright", Polarity::bright}, {"either", Polarity::either}}};

// An integer setting, as an int32.
int32 whole(ics::config::Reader& tracker,
            const char* const key,
            const int64 min,
            const int64 max)
{
  return static_cast<int32>(tracker.integer(key, min, max));
}

ics::tracker::Camera_settings read_camera(ics::config::Reader& tracker)
{
  ics::tracker::Camera_settings camera{};
  for (const std::string& feature : tracker.texts("features", 0U, max_features))
  {
    camera.features += (camera.features.empty() ? "" : " ") + feature;
  }
  camera.format = tracker.choice("pixel_format", formats);
  camera.region.x = whole(tracker, "region_x", 0, max_pixels);
  camera.region.y = whole(tracker, "region_y", 0, max_pixels);
  camera.region.width = whole(tracker, "region_width", 1, max_pixels);
  camera.region.height = whole(tracker, "region_height", 1, max_pixels);
  camera.frame_rate_hz = tracker.number("frame_rate_hz", min_frame_rate_hz, max_frame_rate_hz);
  camera.exposure_us = tracker.number("exposure_us", 1.0, max_exposure_us);
  camera.buffers = whole(tracker, "buffers", 2, ics::tracker::max_buffers);
  camera.buffer_bytes = 0;
  camera.stamps = tracker.choice("time_stamps", stamps);
  camera.stamp_offset = tracker.duration("stamp_offset_ns", ics::Duration::zero(), max_stamp_offset);
  return camera;
}

ics::tracker::Camera_geometry read_geometry(ics::config::Reader& tracker)
{
  ics::tracker::Camera_geometry geometry{};
  geometry.boresight_x = tracker.number("boresight_x", 0.0, max_count);
  geometry.boresight_y = tracker.number("boresight_y", 0.0, max_count);
  geometry.plate_scale_rad =
    tracker.number("plate_scale_urad", min_plate_scale_urad, max_plate_scale_urad) * radians_per_microradian;
  const ics::Degrees roll =
    tracker.degrees("roll_deg", ics::Degrees(-full_circle_deg), ics::Degrees(full_circle_deg));
  geometry.roll_rad = ics::to_radians(roll).value();
  geometry.mirrored = tracker.boolean("mirrored");
  return geometry;
}

ics::tracker::Centroid_settings read_centroid(ics::config::Reader& tracker)
{
  ics::tracker::Centroid_settings centroid{};
  centroid.polarity = tracker.choice("polarity", polarities);
  centroid.k_sigma = tracker.number("k_sigma", 0.0, max_k_sigma);
  centroid.min_contrast = tracker.number("min_contrast", 0.0, max_count);
  centroid.min_area = whole(tracker, "min_area", 1, max_pixels);
  centroid.max_area = whole(tracker, "max_area", 1, max_pixels);
  return centroid;
}

ics::tracker::Tracker_settings read_tracking(ics::config::Reader& tracker)
{
  ics::tracker::Tracker_settings settings{};
  settings.camera = read_geometry(tracker);
  settings.centroid = read_centroid(tracker);
  const int32 acquire =
    whole(tracker, "acquire_window_px", ics::tracker::min_window_side, ics::tracker::max_window_side);
  const int32 track = whole(tracker, "track_window_px", ics::tracker::min_window_side, ics::tracker::max_window_side);
  settings.acquire_window = {acquire, acquire};
  settings.track_window = {track, track};
  settings.gate_px = tracker.number("gate_px", min_gate_px, static_cast<float64>(ics::tracker::max_window_side));
  settings.confirm_frames = whole(tracker, "confirm_frames", 1, max_confirm_frames);
  settings.estimator.alpha = tracker.number("alpha", 0.0, 1.0);
  settings.estimator.beta = tracker.number("beta", 0.0, max_beta);
  settings.coast_limit = tracker.duration("coast_limit_ns", ics::Duration::zero(), max_coast);
  settings.command_latency = tracker.duration("command_latency_ns", ics::Duration::zero(), max_latency);
  settings.gain = tracker.number("gain", 0.0, max_gain);
  return settings;
}

}  // namespace

namespace ics::tracker
{

Tracker_config read_tracker_settings(config::Reader& tracker)
{
  Tracker_config config{};
  config.camera_id = tracker.text("camera");
  config.fake_camera = tracker.boolean("fake_camera");
  config.camera = read_camera(tracker);
  config.tracker = read_tracking(tracker);
  return config;
}

Tracker_config read_tracker_config(config::Reader& root)
{
  config::Reader tracker = root.section("tracker");
  return read_tracker_settings(tracker);
}

}  // namespace ics::tracker
