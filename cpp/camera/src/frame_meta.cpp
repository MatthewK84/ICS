#include "ics/camera/frame_meta.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "ics/camera/cine.hpp"
#include "ics/common/units.hpp"
#include "ics/v1/camera_frame_meta.pb.h"
#include "ics/v1/time_quality.pb.h"

namespace ics::camera {

TimeAuthority time_authority(const bool irig_configured, const v1::TimeQuality& quality,
                             const std::string_view camera_id) {
  TimeAuthority out{.irig_configured = irig_configured, .irig_locked = quality.irig_b_locked()};
  std::int64_t measured = 0;
  bool found = false;
  for (const v1::TimeQuality::CameraOffset& offset : quality.camera_offsets()) {
    const bool newer = offset.camera_id() == camera_id && (!found || offset.measured_utc_ns() >= measured);
    out.camera_offset = newer ? Duration(offset.offset_ns()) : out.camera_offset;
    measured = newer ? offset.measured_utc_ns() : measured;
    found = found || newer;
  }
  return out;
}

std::vector<v1::CameraFrameMeta> frame_meta(const Cine& cine, const SegmentSource& source,
                                            const TimeAuthority& authority) {
  std::vector<v1::CameraFrameMeta> out;
  out.reserve(cine.frame_times.size());
  for (std::size_t i = 0; i < cine.frame_times.size(); ++i) {
    v1::CameraFrameMeta& frame = out.emplace_back();
    frame.set_station_id(source.station_id);
    frame.set_camera_id(source.camera_id);
    frame.set_camera_kind(source.camera_kind);
    frame.set_segment_id(source.segment_id);
    frame.set_frame_index(i);
    frame.set_exposure_start_utc_ns(to_utc_ns(cine.frame_times[i] - authority.camera_offset));
    frame.set_exposure_duration_ns((i < cine.exposures.size() ? cine.exposures[i] : cine.exposure).count());
    frame.set_time_source(authority.irig() ? v1::CameraFrameMeta::TIME_SOURCE_IRIG
                                           : v1::CameraFrameMeta::TIME_SOURCE_HOST);
    frame.set_time_offset_applied_ns(authority.camera_offset.count());
    frame.set_width_px(cine.width);
    frame.set_height_px(cine.height);
    frame.set_bits_per_pixel(cine.real_bpp != 0 ? cine.real_bpp : cine.bit_count);
    frame.set_window_x_px(source.window_x);
    frame.set_window_y_px(source.window_y);
  }
  return out;
}

}  // namespace ics::camera
