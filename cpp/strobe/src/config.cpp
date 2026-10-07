#include "ics/strobe/config.hpp"

#include <chrono>
#include <cstdint>

#include "ics/camera/strobe.hpp"
#include "ics/common/units.hpp"
#include "ics/config/logging_config.hpp"
#include "ics/config/reader.hpp"

namespace ics::strobe {
namespace {

using std::chrono::microseconds;
using std::chrono::milliseconds;
using std::chrono::seconds;

constexpr std::int64_t kMaxPixel = 65'535;
constexpr std::int64_t kMaxSweepSteps = 3'600;

[[nodiscard]] camera::Roi read_roi(config::Reader& strobe) {
  return camera::Roi{.x = static_cast<std::uint32_t>(strobe.integer("roi_x_px", 0, kMaxPixel)),
                     .y = static_cast<std::uint32_t>(strobe.integer("roi_y_px", 0, kMaxPixel)),
                     .width = static_cast<std::uint32_t>(strobe.integer("roi_width_px", 1, kMaxPixel)),
                     .height = static_cast<std::uint32_t>(strobe.integer("roi_height_px", 1, kMaxPixel))};
}

[[nodiscard]] camera::StrobeSchedule read_schedule(config::Reader& strobe) {
  return camera::StrobeSchedule{
      .pulse_width = strobe.duration("pulse_width_ns", Duration(1), seconds(1)),
      .latency = strobe.duration("latency_ns", Duration::zero(), seconds(1)),
      .delay_step = strobe.duration("delay_step_ns", Duration::zero(), seconds(1)),
      .sweep_steps = static_cast<std::uint32_t>(strobe.integer("sweep_steps", 1, kMaxSweepSteps))};
}

}  // namespace

AnalyzerConfig read_analyzer_config(config::Reader& root) {
  AnalyzerConfig out;
  out.log = config::read_logging(root);
  config::Reader strobe = root.section("strobe");
  out.station_id = strobe.text("station_id");
  out.strobe.camera_id = strobe.text("camera_id");
  out.offsets_file = strobe.text("offsets_file");
  out.strobe.roi = read_roi(strobe);
  out.strobe.schedule = read_schedule(strobe);
  out.strobe.max_offset = strobe.duration("max_offset_ns", microseconds(1), milliseconds(100));
  return out;
}

}  // namespace ics::strobe
