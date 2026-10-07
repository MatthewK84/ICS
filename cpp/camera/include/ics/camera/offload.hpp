#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "ics/camera/cine.hpp"
#include "ics/camera/frame_meta.hpp"
#include "ics/camera/phantom.hpp"
#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/v1/camera_frame_meta.pb.h"

namespace ics::camera {

// ICS-027's "Done when": back-to-back segments offload with verified times.

// The most consecutive frames' spacing may differ from one frame period.
inline constexpr Duration kSpacingTolerance = std::chrono::microseconds(1);

// What to record, and where to save it.
struct OffloadPlan {
  std::string station_id;
  std::string camera_id;
  std::uint32_t segments = 0;
  // The frames of each segment to keep: the rest are trimmed.
  FrameRange frames{};
  std::filesystem::path directory;
};

// What a saved segment should hold.
struct SegmentExpectation {
  FrameRange frames{};
  // When the camera reported the segment's trigger.
  UtcTime trigger_time{};
  // The last frame of the segment saved before it, if any.
  std::optional<UtcTime> previous_last;
  TimeAuthority authority{};
};

// One saved segment, and how its times verified.
struct SegmentCheck {
  std::uint32_t segment = 0;
  std::filesystem::path path;
  std::size_t frames = 0;
  UtcTime first{};
  UtcTime last{};
  // The largest difference between consecutive frames' spacing and a frame
  // period.
  Duration spacing_error{};
  // The larger of how far the cine's trigger time is from the one the camera
  // reported, and how far its first frame is from where the trigger and its
  // number put it.
  Duration trigger_error{};
  // Whether its frames are IRIG-timed (TimeAuthority::irig).
  bool irig = false;
  // Whether it starts after the segment before it ends.
  bool ordered = false;
  // Whether it holds the frames planned, is IRIG-timed and ordered, its
  // spacing is within kSpacingTolerance and its trigger within a frame period.
  bool verified = false;
  std::vector<v1::CameraFrameMeta> meta;
};

struct OffloadReport {
  std::vector<SegmentCheck> segments;
  // Whether every segment verified.
  bool verified = false;
};

// Checks a saved segment's times against what it should hold.
[[nodiscard]] SegmentCheck verify_segment(const Cine& cine, const SegmentExpectation& expected);

// Configures the camera, arms it for the plan's segments and triggers each
// in turn, noting the station's time quality as each is recorded; then saves
// the plan's frames of each segment as "cine-NNN.cine" in the plan's
// directory, reads each back and verifies its times. Fails as the camera
// does, and with read_cine's errors for a cine that cannot be read back.
[[nodiscard]] Result<OffloadReport> record_and_offload(PhantomCamera& camera, const CameraSettings& settings,
                                                       const OffloadPlan& plan, TimeQualitySource& quality);

}  // namespace ics::camera
