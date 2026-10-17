#pragma once

#include <cstdint>

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/encoder/pointing_model.hpp"
#include "ics/encoder/track.hpp"
#include "ics/v1/camera_frame_meta.pb.h"

namespace ics::encoder {

// Where a camera frame looked: its line of sight at mid-exposure (ICS-036),
// for ics-mountd (ICS-041) and registration (CPP-23).
struct FrameLineOfSight {
  std::uint64_t frame_index = 0;
  // The exposure's start plus half its duration.
  UtcTime mid_exposure;
  // The line of sight: the pointing model applied to the axis angles.
  Direction sky;
  // The axis angles and rates at mid-exposure, with their time bound, clock
  // state and bound on the sky (Interpolated::bound_rad).
  Interpolated axes;
};

// The frame's line of sight. Only frames timed by IRIG-B are taken: a frame
// stamped by the host on arrival is kUnconstrained, since at 0.1 rad/s a
// millisecond is 100 urad. kInvalidArgument for a negative exposure, or one
// whose middle is past the last time a UtcTime holds; otherwise as
// AngleTrack::at, so a frame outside the history is kOutOfRange and one in a
// gap of more than kMaxGap is kUnconstrained.
[[nodiscard]] Result<FrameLineOfSight> line_of_sight(const AngleTrack& track, const PointingModel& model,
                                                     const v1::CameraFrameMeta& frame) noexcept;

}  // namespace ics::encoder
