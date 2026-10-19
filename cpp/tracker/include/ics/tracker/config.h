// config.h: the [tracker] table of a station's config (ICS-038), the
// production input of the tracker and its camera. ics-tracker-monitor reads
// it from /etc/ics/ics-tracker.toml, and ics-mountd (ICS-041) will read the
// same table. cpp/tracker/ics-tracker.toml is a sample with every setting:
//
//   [tracker]
//   camera = "Basler-21234567"        # Aravis's ID for the camera: <vendor>-<serial>, or Fake_1
//   fake_camera = false               # true enables Aravis's fake camera, for tests
//   features = ["PtpEnable=true"]     # GenICam features set first, as Name=Value; [] for none
//   pixel_format = "mono16"           # or "mono8"
//   region_x = 0                      # the part of the sensor read out, in pixels
//   region_y = 0
//   region_width = 640
//   region_height = 480
//   frame_rate_hz = 100.0
//   exposure_us = 2000.0
//   buffers = 8                       # 2 to 32
//   time_stamps = "camera"            # the camera's PTP stamps; "host" for the host's receive time
//   stamp_offset_ns = 37_000_000_000  # subtracted from each stamp: TAI - UTC, or the receive latency
//   boresight_x = 320.0               # the pixel the pointing model's line of sight falls on
//   boresight_y = 240.0
//   plate_scale_urad = 20.0           # microradians a pixel
//   roll_deg = 0.0                    # from the image's right to increasing azimuth, counterclockwise
//   mirrored = false                  # whether the optics mirror the image
//   polarity = "either"               # "dark" by day, "bright" by night, or "either"
//   k_sigma = 5.0                     # how far a pixel stands out: noise sigmas,
//   min_contrast = 50.0               # or counts, whichever is larger
//   min_area = 3                      # pixels in a target
//   max_area = 400
//   acquire_window_px = 128           # the side of the window searched while cueing, 3 to 256
//   track_window_px = 48              # and once there is a track
//   gate_px = 8.0                     # how far a detection may be from the track's prediction
//   confirm_frames = 5                # detections in a row that confirm a track
//   alpha = 0.5                       # the track's gains
//   beta = 0.1
//   coast_limit_ns = 500_000_000      # how long a track is carried on without a detection
//   command_latency_ns = 50_000_000   # from sending a mount command to its taking effect
//   gain = 2.0                        # how fast the demand closes the error, per second
#ifndef ICS_TRACKER_CONFIG_H
#define ICS_TRACKER_CONFIG_H

// JSF deviation D-6: ics::config's Reader comes from its reader.hpp.
#include <string>

#include <ics/config/reader.hpp>
#include <ics/tracker/camera.h>
#include <ics/tracker/tracker.h>

namespace ics::tracker
{

// Everything a station's tracker reads.
struct Tracker_config
{
  std::string camera_id;
  bool fake_camera;
  Camera_settings camera;
  Tracker_settings tracker;
};

// The [tracker] table. Like every config::Reader schema, it records each
// missing or invalid setting and returns placeholders; use the config only
// once config::Reader::finish() finds no errors, as config::read does.
[[nodiscard]] Tracker_config read_tracker_settings(config::Reader& tracker);

// A config file with the [tracker] table, and nothing else.
[[nodiscard]] Tracker_config read_tracker_config(config::Reader& root);

}  // namespace ics::tracker

#endif  // ICS_TRACKER_CONFIG_H
