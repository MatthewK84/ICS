#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>

#include "ics/camera/emulated_phantom.hpp"
#include "ics/camera/emulated_x6980.hpp"
#include "ics/camera/flir_camera.hpp"
#include "ics/camera/offload.hpp"
#include "ics/camera/segment_camera.hpp"
#include "ics/camera/strobe.hpp"
#include "ics/camera/trigger_schedule.hpp"
#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/strobe/analyze.hpp"
#include "ics/v1/camera_frame_meta.pb.h"
#include "ics/v1/time_quality.pb.h"

// Strobe calibrations recorded on the emulated cameras (ICS-029), for the
// tests.
namespace ics::strobe::testing {

using std::chrono::microseconds;
using std::chrono::milliseconds;
using std::chrono::seconds;

// A second that starts a sweep: a multiple of 40.
inline constexpr std::int64_t kSweepStart = 1'791'331'200;

class LockedQuality final : public camera::TimeQualitySource {
 public:
  v1::TimeQuality current() override {
    v1::TimeQuality quality;
    quality.set_irig_b_locked(true);
    quality.set_time_utc_ns(kSweepStart * 1'000'000'000);
    return quality;
  }
};

// A calibration's camera and strobe.
struct Calibration {
  camera::CameraSettings settings;
  camera::StrobeScene scene;
};

// The Phantom: 5,000 frames/s, 150 µs exposures, stamped 37.4 µs late; a
// 20 µs pulse swept by 5 µs a second over 40 seconds.
inline Calibration phantom() {
  return Calibration{
      .settings = {.width = 32, .height = 32, .window_x = 0, .window_y = 0, .frame_rate = 5000,
                   .exposure = microseconds(150), .segment_frames = 20, .post_trigger_frames = 10, .irig = true},
      .scene = {.schedule = {.pulse_width = microseconds(20), .latency = Duration(1500),
                             .delay_step = microseconds(5), .sweep_steps = 40},
                .roi = {.x = 8, .y = 8, .width = 8, .height = 8},
                .stamp_offset = Duration(37'400),
                .background = 200.0, .gain_per_us = 50.0, .noise_sigma = 30.0, .seed = 1}};
}

// The X6980: 1,004 frames/s, 800 µs integration, stamped 112.6 µs early to
// the microsecond; a 30 µs pulse swept by 25 µs a second over 40 seconds.
inline Calibration x6980() {
  return Calibration{
      .settings = {.width = 32, .height = 32, .window_x = 300, .window_y = 200, .frame_rate = 1004,
                   .exposure = microseconds(800), .segment_frames = 20, .post_trigger_frames = 10, .irig = true},
      .scene = {.schedule = {.pulse_width = microseconds(30), .latency = Duration(1500),
                             .delay_step = microseconds(25), .sweep_steps = 40},
                .roi = {.x = 8, .y = 8, .width = 8, .height = 8},
                .stamp_offset = Duration(-112'600),
                .background = 2000.0, .gain_per_us = 100.0, .noise_sigma = 60.0, .seed = 2}};
}

// 40 segments, one triggered at each PPS, saved to folder as cines.
inline camera::OffloadPlan plan(const std::filesystem::path& folder, const v1::CameraFrameMeta::CameraKind kind) {
  return camera::OffloadPlan{.station_id = "station-1", .camera_id = "camera-1", .camera_kind = kind,
                             .segments = 40, .frames = {.first = -10, .count = 20}, .directory = folder};
}

inline camera::TriggerSchedule pps() {
  return camera::TriggerSchedule{.first = UtcTime(seconds(kSweepStart)), .interval = seconds(1)};
}

inline Status record_phantom(const std::filesystem::path& folder, const Calibration& c) {
  camera::EmulatedPhantom camera(pps(), c.scene);
  LockedQuality quality;
  const Result<camera::OffloadReport> report = camera::record_and_offload(
      camera, c.settings, plan(folder, v1::CameraFrameMeta::CAMERA_KIND_HIGH_SPEED_VISIBLE), quality);
  return report ? Status() : fail(report.error());
}

inline Status record_x6980(const std::filesystem::path& folder, const Calibration& c) {
  camera::EmulatedX6980 sdk(pps(), true, c.scene);
  LockedQuality quality;
  camera::FlirCamera camera(sdk, quality);
  const Result<camera::OffloadReport> report =
      camera::record_and_offload(camera, c.settings, plan(folder, v1::CameraFrameMeta::CAMERA_KIND_MWIR), quality);
  return report ? Status() : fail(report.error());
}

inline StrobeSettings settings_for(const Calibration& c) {
  return StrobeSettings{
      .camera_id = "camera-1", .roi = c.scene.roi, .schedule = c.scene.schedule, .max_offset = microseconds(500)};
}

}  // namespace ics::strobe::testing
