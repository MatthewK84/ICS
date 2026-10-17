#include "ics/encoder/line_of_sight.hpp"

#include <cstdint>
#include <limits>

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/encoder/pointing_model.hpp"
#include "ics/encoder/track.hpp"
#include "ics/v1/camera_frame_meta.pb.h"

namespace ics::encoder {

Result<FrameLineOfSight> line_of_sight(const AngleTrack& track, const PointingModel& model,
                                       const v1::CameraFrameMeta& frame) noexcept {
  if (frame.time_source() != v1::CameraFrameMeta::TIME_SOURCE_IRIG) {
    return fail(Error::kUnconstrained);
  }
  const std::int64_t half = frame.exposure_duration_ns() / 2;
  if (frame.exposure_duration_ns() < 0 || frame.exposure_start_utc_ns() > std::numeric_limits<std::int64_t>::max() - half) {
    return fail(Error::kInvalidArgument);
  }
  const UtcTime middle = utc_from_ns(frame.exposure_start_utc_ns() + half);
  const Result<Interpolated> axes = track.at(middle);
  if (!axes) {
    return fail(axes.error());
  }
  return FrameLineOfSight{
      .frame_index = frame.frame_index(), .mid_exposure = middle, .sky = to_sky(model, axes->encoder), .axes = *axes};
}

}  // namespace ics::encoder
