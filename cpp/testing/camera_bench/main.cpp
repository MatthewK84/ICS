// ics-camera-bench (ICS-027, ICS-028): records ten back-to-back segments on
// an emulated camera, a Phantom or a FLIR X6980-HS, saves each as
// cine-NNN.cine in the working directory and verifies its times. The
// directory is not an argument: CodeQL's path-injection rule rejects a path
// taken from the command line. Prints a JSON line per segment, then one with
// the verdict; exits 0 when every segment verified, 1 when one did not or
// the camera failed.
//
// Usage: ics-camera-bench phantom|x6980

#include <chrono>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <span>
#include <string_view>
#include <system_error>

#include "ics/camera/emulated_phantom.hpp"
#include "ics/camera/emulated_x6980.hpp"
#include "ics/camera/flir_camera.hpp"
#include "ics/camera/offload.hpp"
#include "ics/camera/segment_camera.hpp"
#include "ics/camera/trigger_schedule.hpp"
#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/v1/camera_frame_meta.pb.h"
#include "ics/v1/time_quality.pb.h"

namespace {

using ics::camera::CameraSettings;
using ics::camera::OffloadPlan;
using ics::camera::OffloadReport;
using std::chrono::microseconds;
using std::chrono::milliseconds;

// The emulated cameras' clocks follow IRIG-B, as a locked station's do, and
// the station's UTC is the host's.
class LockedQuality final : public ics::camera::TimeQualitySource {
 public:
  ics::v1::TimeQuality current() override {
    ics::v1::TimeQuality quality;
    quality.set_irig_b_locked(true);
    quality.set_time_utc_ns(ics::to_utc_ns(std::chrono::time_point_cast<ics::Duration>(std::chrono::system_clock::now())));
    return quality;
  }
};

// 500 frames a segment, 400 of them after the trigger; 300 of each saved.
CameraSettings settings(const std::uint32_t frame_rate, const ics::Duration exposure) {
  return CameraSettings{.width = 64,
                        .height = 32,
                        .window_x = 0,
                        .window_y = 0,
                        .frame_rate = frame_rate,
                        .exposure = exposure,
                        .segment_frames = 500,
                        .post_trigger_frames = 400,
                        .irig = true};
}

OffloadPlan plan(const std::filesystem::path& directory, const ics::v1::CameraFrameMeta::CameraKind kind) {
  return OffloadPlan{.station_id = "bench",
                     .camera_id = "emulated",
                     .camera_kind = kind,
                     .segments = 10,
                     .frames = ics::camera::FrameRange{.first = -50, .count = 300},
                     .directory = directory};
}

// Triggers from the next whole second.
ics::camera::TriggerSchedule schedule(const ics::Duration interval) {
  return {.first = std::chrono::ceil<std::chrono::seconds>(std::chrono::system_clock::now()), .interval = interval};
}

// 5,000 frames/s; triggers 120 ms apart, each segment 100 ms.
ics::Result<OffloadReport> phantom(const std::filesystem::path& here) {
  ics::camera::EmulatedPhantom camera(schedule(milliseconds(120)));
  LockedQuality quality;
  return ics::camera::record_and_offload(camera, settings(5000, microseconds(150)),
                                         plan(here, ics::v1::CameraFrameMeta::CAMERA_KIND_HIGH_SPEED_VISIBLE), quality);
}

// 1,004 frames/s in a window at (288, 240), stamped without the year;
// triggers 600 ms apart, each segment 498 ms.
ics::Result<OffloadReport> x6980(const std::filesystem::path& here) {
  ics::camera::EmulatedX6980 sdk(schedule(milliseconds(600)), false);
  LockedQuality quality;
  ics::camera::FlirCamera camera(sdk, quality);
  CameraSettings s = settings(1004, microseconds(800));
  s.window_x = 288;
  s.window_y = 240;
  return ics::camera::record_and_offload(camera, s, plan(here, ics::v1::CameraFrameMeta::CAMERA_KIND_MWIR), quality);
}

void print(const ics::camera::SegmentCheck& s) {
  std::printf(
      "{\"segment\":%" PRIu32 ",\"file\":\"%s\",\"frames\":%zu,\"first_utc_ns\":%" PRId64 ",\"last_utc_ns\":%" PRId64
      ",\"spacing_error_ns\":%" PRId64 ",\"trigger_error_ns\":%" PRId64
      ",\"irig\":%s,\"ordered\":%s,\"verified\":%s}\n",
      s.segment, s.path.filename().c_str(), s.frames, ics::to_utc_ns(s.first), ics::to_utc_ns(s.last),
      static_cast<std::int64_t>(s.spacing_error.count()), static_cast<std::int64_t>(s.trigger_error.count()),
      s.irig ? "true" : "false", s.ordered ? "true" : "false", s.verified ? "true" : "false");
}

int report(const ics::Result<OffloadReport>& result) {
  if (!result) {
    const std::string_view reason = ics::to_string(result.error());
    std::fprintf(stderr, "the camera failed: %.*s\n", static_cast<int>(reason.size()), reason.data());
    return 1;
  }
  for (const ics::camera::SegmentCheck& s : result->segments) {
    print(s);
  }
  std::printf("{\"verified\":%s}\n", result->verified ? "true" : "false");
  return result->verified ? 0 : 1;
}

}  // namespace

int main(const int argc, char* argv[]) {
  const std::span<const char* const> args(argv, static_cast<std::size_t>(argc));
  const std::string_view camera = args.size() == 2 ? std::string_view(args[1]) : std::string_view();
  if (camera != "phantom" && camera != "x6980") {
    std::fputs("usage: ics-camera-bench phantom|x6980\n", stderr);
    return 2;
  }
  std::error_code error;
  const std::filesystem::path here = std::filesystem::current_path(error);
  if (error) {
    std::fputs("cannot find the working directory\n", stderr);
    return 1;
  }
  return report(camera == "phantom" ? phantom(here) : x6980(here));
}
