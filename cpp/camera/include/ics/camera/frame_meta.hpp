#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "ics/camera/cine.hpp"
#include "ics/common/units.hpp"
#include "ics/v1/camera_frame_meta.pb.h"
#include "ics/v1/time_quality.pb.h"

namespace ics::camera {

// How a segment's frames were timed, from what ICS knows rather than what
// the cine says: whether the configuration ICS applied, as the camera
// reported it back, has the IRIG-B input on; whether the station's IRIG-B
// was locked to the grandmaster when the segment was recorded
// (TimeQuality.irig_b_locked); and the camera's measured offset
// (TimeQuality.CameraOffset), which is 0 before the camera is calibrated.
struct TimeAuthority {
  bool irig_configured = false;
  bool irig_locked = false;
  Duration camera_offset{};

  // Frames are IRIG-timed only when both say so.
  [[nodiscard]] bool irig() const noexcept { return irig_configured && irig_locked; }
};

// The authority for a camera from the station's time quality: its offset is
// the most recently measured one for the camera, if any.
[[nodiscard]] TimeAuthority time_authority(bool irig_configured, const v1::TimeQuality& quality,
                                           std::string_view camera_id);

// What a segment's frames carry to name them.
struct SegmentName {
  std::string station_id;
  std::string camera_id;
  std::string segment_id;
};

// Each saved frame's metadata. A frame's exposure starts at its cine time
// less the camera offset: the strobe analyzer (ICS-029) measures that offset
// from a strobe's frame to its true time, so it also takes up where in the
// exposure the camera stamps a frame. An IRIG-timed frame is
// TIME_SOURCE_IRIG; any other is TIME_SOURCE_HOST, and does not verify.
[[nodiscard]] std::vector<v1::CameraFrameMeta> frame_meta(const Cine& cine, const SegmentName& name,
                                                          const TimeAuthority& authority);

}  // namespace ics::camera
