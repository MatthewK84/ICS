// ics-phantom-bench (ICS-027): records ten back-to-back segments on the
// emulated Phantom, saves each as cine-NNN.cine in the working directory and
// verifies its times. The directory is not an argument: CodeQL's
// path-injection rule rejects a path taken from the command line. Prints a
// JSON line per segment, then one with the verdict; exits 0 when every
// segment verified, 1 when one did not or the camera failed.
//
// Usage: ics-phantom-bench

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
#include "ics/camera/offload.hpp"
#include "ics/camera/phantom.hpp"
#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/v1/time_quality.pb.h"

namespace {

using std::chrono::microseconds;
using std::chrono::milliseconds;

constexpr std::uint32_t kSegments = 10;

// The emulated camera's clock follows IRIG-B, as a locked station's does.
class LockedQuality final : public ics::camera::TimeQualitySource {
 public:
  LockedQuality() { quality_.set_irig_b_locked(true); }
  ics::v1::TimeQuality current() override { return quality_; }

 private:
  ics::v1::TimeQuality quality_;
};

// 5,000 frames/s, 500 frames a segment, 400 of them after the trigger.
ics::camera::CameraSettings settings() {
  return ics::camera::CameraSettings{.width = 64,
                                     .height = 32,
                                     .frame_rate = 5000,
                                     .exposure = microseconds(150),
                                     .segment_frames = 500,
                                     .post_trigger_frames = 400,
                                     .irig = true};
}

ics::camera::OffloadPlan plan(const std::filesystem::path& directory) {
  return ics::camera::OffloadPlan{.station_id = "bench",
                                  .camera_id = "phantom-emulated",
                                  .segments = kSegments,
                                  .frames = ics::camera::FrameRange{.first = -50, .count = 300},
                                  .directory = directory};
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

}  // namespace

int main(const int argc, char* argv[]) {
  const std::span<const char* const> args(argv, static_cast<std::size_t>(argc));
  if (args.size() != 1) {
    std::fputs("usage: ics-phantom-bench\n", stderr);
    return 2;
  }
  std::error_code error;
  const std::filesystem::path here = std::filesystem::current_path(error);
  if (error) {
    std::fputs("cannot find the working directory\n", stderr);
    return 1;
  }
  // Triggers 120 ms apart from the next whole second; each segment is 100 ms.
  const ics::UtcTime first = std::chrono::ceil<std::chrono::seconds>(std::chrono::system_clock::now());
  ics::camera::EmulatedPhantom camera(ics::camera::TriggerSchedule{.first = first, .interval = milliseconds(120)});
  LockedQuality quality;
  const ics::Result<ics::camera::OffloadReport> report =
      ics::camera::record_and_offload(camera, settings(), plan(here), quality);
  if (!report) {
    const std::string_view reason = ics::to_string(report.error());
    std::fprintf(stderr, "the camera failed: %.*s\n", static_cast<int>(reason.size()), reason.data());
    return 1;
  }
  for (const ics::camera::SegmentCheck& s : report->segments) {
    print(s);
  }
  std::printf("{\"verified\":%s}\n", report->verified ? "true" : "false");
  return report->verified ? 0 : 1;
}
