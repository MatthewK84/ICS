#include "ics/camd/config.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>

#include "ics/common/check.hpp"

namespace ics::camd {
namespace {

constexpr std::int64_t kMaxPixels = 65'535;
constexpr std::int64_t kMaxFrameRate = 1'000'000;
constexpr std::int64_t kMaxFrames = std::int64_t{1} << 20;
constexpr std::int64_t kMaxSegments = 64;
constexpr std::size_t kMaxFolderName = 64;
constexpr Duration kMaxExposure = std::chrono::seconds(1);
constexpr Duration kMaxTimeWait = std::chrono::seconds(10);

[[nodiscard]] std::uint32_t count(config::Reader& table, const std::string_view key, const std::int64_t min,
                                  const std::int64_t max) {
  return static_cast<std::uint32_t>(table.integer(key, min, max));
}

// The settings a camera's table gives it.
[[nodiscard]] camera::CameraSettings read_settings(config::Reader& table) {
  return camera::CameraSettings{.width = count(table, "width_px", 1, kMaxPixels),
                                .height = count(table, "height_px", 1, kMaxPixels),
                                .window_x = count(table, "window_x_px", 0, kMaxPixels),
                                .window_y = count(table, "window_y_px", 0, kMaxPixels),
                                .frame_rate = count(table, "frame_rate_hz", 1, kMaxFrameRate),
                                .exposure = table.duration("exposure_ns", Duration(1), kMaxExposure),
                                .segment_frames = count(table, "segment_frames", 1, kMaxFrames),
                                .post_trigger_frames = count(table, "post_trigger_frames", 0, kMaxFrames),
                                .irig = table.boolean("irig")};
}

[[nodiscard]] CameraConfig read_camera(config::Reader& cameras, const std::string& id) {
  config::Reader table = cameras.section(id);
  CameraConfig out{.id = id, .settings = read_settings(table), .segments = 0, .frames = {}, .driver = {}};
  out.segments = count(table, "segments", 1, kMaxSegments);
  out.frames.first = static_cast<std::int32_t>(table.integer("first_frame", -kMaxFrames, kMaxFrames));
  out.frames.count = count(table, "frame_count", 1, kMaxFrames);
  config::Reader driver = table.section("driver");
  out.driver = read_driver(driver);
  return out;
}

[[nodiscard]] bool folder_char(const char c) noexcept {
  const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
  return letter || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
}

}  // namespace

Config read_config(config::Reader& root) {
  Config out;
  out.log = config::read_logging(root);
  config::Reader camd = root.section("camd");
  out.station_id = camd.text("station_id");
  out.command_socket = camd.text("command_socket");
  out.time_socket = camd.text("time_socket");
  out.offload_folder = camd.text("offload_folder");
  out.time_wait = camd.duration("time_wait_ns", Duration::zero(), kMaxTimeWait);
  const std::vector<std::string> ids = camd.texts("cameras", 1, kMaxCameras);
  config::Reader cameras = root.section("camera");
  out.cameras.reserve(ids.size());
  for (const std::string& id : ids) {
    out.cameras.push_back(read_camera(cameras, id));
  }
  return out;
}

bool folder_name(const std::string_view name) noexcept {
  const bool sized = !name.empty() && name.size() <= kMaxFolderName;
  return sized && name != "." && name != ".." && std::ranges::all_of(name, folder_char);
}

Status validate(const Config& config, std::string& reason) {
  // read_config reads at most kMaxCameras.
  static_cast<void>(ics::check(config.cameras.size() <= kMaxCameras));
  std::set<std::string_view> seen;
  for (const CameraConfig& camera : config.cameras) {
    if (!folder_name(camera.id)) {
      reason = "camera ID \"" + camera.id + "\" must be 1 to 64 letters, digits, '.', '_' or '-'";
      return fail(Error::kInvalidArgument);
    }
    if (!seen.insert(camera.id).second) {
      reason = "camera ID \"" + camera.id + "\" is listed twice";
      return fail(Error::kInvalidArgument);
    }
  }
  return {};
}

}  // namespace ics::camd
