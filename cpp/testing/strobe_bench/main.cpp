// ics-strobe-bench (ICS-029): records a strobe calibration, 40 segments of a
// swept PPS strobe, on an emulated camera whose stamps carry a known offset;
// saves them as cines in the working folder; measures the offset; publishes
// it to camera-offsets.binpb there and reads it back as ics-timingd would.
// The folder is not an argument: CodeQL's path-injection rule rejects a path
// taken from the command line. Prints a JSON line; exits 0 when the offset
// and its sigma are within 5 µs, 1 when not or a step failed.
//
// Usage: ics-strobe-bench phantom|x6980

#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <span>
#include <string_view>
#include <system_error>

#include "ics/common/error.hpp"
#include "ics/strobe/analyze.hpp"
#include "ics/timing/camera_offsets.hpp"
#include "strobe_rig.hpp"

namespace {

namespace rig = ics::strobe::testing;

constexpr std::int64_t kLimitNs = 5'000;

// Records the calibration, measures it, and publishes the offset.
ics::Result<ics::strobe::StrobeReport> calibrate(const std::string_view camera, const std::filesystem::path& here,
                                                 const rig::Calibration& c) {
  const ics::Status recorded = camera == "phantom" ? rig::record_phantom(here, c) : rig::record_x6980(here, c);
  const ics::Result<ics::strobe::StrobeReport> report =
      recorded ? ics::strobe::analyze_folder(here, rig::settings_for(c)) : ics::fail(recorded.error());
  const ics::Status published =
      report ? ics::strobe::publish(report->offset, "station-1", here / "camera-offsets.binpb") : ics::fail(report.error());
  return published ? report : ics::fail(published.error());
}

int print(const std::string_view camera, const rig::Calibration& c, const ics::strobe::StrobeReport& report,
          const ics::timing::CameraOffsets& read) {
  const std::int64_t injected = c.scene.stamp_offset.count();
  const std::int64_t error = read.offsets.front().offset_ns() - injected;
  const bool within = std::llabs(error) < kLimitNs && read.offsets.front().offset_sigma_ns() < kLimitNs;
  std::printf("{\"camera\":\"%.*s\",\"injected_offset_ns\":%" PRId64 ",\"offset_ns\":%" PRId64
              ",\"offset_sigma_ns\":%" PRId64 ",\"error_ns\":%" PRId64
              ",\"segments\":%zu,\"lit\":%zu,\"edge_frames\":%zu,\"rms\":%.3f,\"within_5us\":%s}\n",
              static_cast<int>(camera.size()), camera.data(), injected, read.offsets.front().offset_ns(),
              read.offsets.front().offset_sigma_ns(), error, report.segments, report.lit, report.fit.edge_frames,
              report.fit.rms, within ? "true" : "false");
  return within ? 0 : 1;
}

}  // namespace

int main(const int argc, char* argv[]) {
  const std::span<const char* const> args(argv, static_cast<std::size_t>(argc));
  const std::string_view camera = args.size() == 2 ? std::string_view(args[1]) : std::string_view();
  if (camera != "phantom" && camera != "x6980") {
    std::fputs("usage: ics-strobe-bench phantom|x6980\n", stderr);
    return 2;
  }
  std::error_code error;
  const std::filesystem::path here = std::filesystem::current_path(error);
  if (error) {
    std::fputs("cannot find the working folder\n", stderr);
    return 1;
  }
  const rig::Calibration c = camera == "phantom" ? rig::phantom() : rig::x6980();
  const ics::Result<ics::strobe::StrobeReport> report = calibrate(camera, here, c);
  const ics::Result<ics::timing::CameraOffsets> read =
      report ? ics::timing::read_camera_offsets(here / "camera-offsets.binpb") : ics::fail(report.error());
  if (!read || read->offsets.empty()) {
    const std::string_view reason = ics::to_string(read ? ics::Error::kEmpty : read.error());
    std::fprintf(stderr, "the calibration failed: %.*s\n", static_cast<int>(reason.size()), reason.data());
    return 1;
  }
  return print(camera, c, *report, *read);
}
