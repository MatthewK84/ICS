#include "ics/camera/flir_camera.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

#include "file_out.hpp"
#include "ics/camera/cine.hpp"
#include "ics/camera/flir_sdk.hpp"
#include "ics/camera/irig.hpp"
#include "ics/camera/segment_camera.hpp"
#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/v1/time_quality.pb.h"

namespace ics::camera {
namespace {

// 16-bit pixels, 14 of their bits filled by the sensor.
constexpr std::uint16_t kBitCount = 16;
constexpr std::uint32_t kRealBpp = 14;
constexpr std::size_t kPixelBytes = 2;

// A segment's frames as a cine, each time converted from its stamp, and
// their pixels in turn.
struct Saved {
  Cine cine;
  std::vector<std::byte> images;
};

[[nodiscard]] Saved empty_cine(const CameraSettings& s, const UtcTime trigger, const FrameRange& frames) {
  Saved out{.cine = Cine{.first_image_no = frames.first,
                         .trigger_time = trigger,
                         .width = s.width,
                         .height = s.height,
                         .bit_count = kBitCount,
                         .real_bpp = kRealBpp,
                         .frame_rate = s.frame_rate,
                         .exposure = s.exposure,
                         .frame_times = {},
                         .exposures = {},
                         .image_offsets = {}},
            .images = {}};
  out.cine.frame_times.reserve(frames.count);
  out.cine.exposures.reserve(frames.count);
  out.images.reserve(std::size_t{s.width} * s.height * kPixelBytes * frames.count);
  return out;
}

// Adds a frame to the cine, if it is the next one and its stamp is a time.
[[nodiscard]] bool add(Saved& saved, const FlirFrame& frame, const std::size_t frame_bytes) {
  const std::int64_t next =
      std::int64_t{saved.cine.first_image_no} + static_cast<std::int64_t>(saved.cine.frame_times.size());
  const Result<UtcTime> time = utc_from_irig(frame.stamp, saved.cine.trigger_time);
  if (frame.number != next || frame.pixels.size() != frame_bytes || !time) {
    return false;
  }
  saved.cine.frame_times.push_back(*time);
  saved.cine.exposures.push_back(frame.integration);
  saved.images.insert(saved.images.end(), frame.pixels.begin(), frame.pixels.end());
  return true;
}

[[nodiscard]] Result<Saved> to_cine(const CameraSettings& s, const UtcTime trigger, const FrameRange& frames,
                                    const std::vector<FlirFrame>& read) {
  Saved out = empty_cine(s, trigger, frames);
  const std::size_t frame_bytes = std::size_t{s.width} * s.height * kPixelBytes;
  // Adds each frame in turn, stopping at the first that does not belong.
  const bool added =
      std::ranges::all_of(read, [&out, frame_bytes](const FlirFrame& frame) { return add(out, frame, frame_bytes); });
  if (!added || out.cine.frame_times.size() != frames.count) {
    return fail(Error::kMalformed);
  }
  return out;
}

}  // namespace

Result<CameraSettings> FlirCamera::configure(const CameraSettings& settings) {
  Result<CameraSettings> applied = sdk_.configure(settings);
  settings_ = applied ? *applied : settings_;
  triggers_.clear();
  return applied;
}

Status FlirCamera::arm(const std::uint32_t segments) {
  triggers_.clear();
  return sdk_.arm(segments);
}

Result<SegmentStatus> FlirCamera::trigger() {
  const Result<FlirTrigger> fired = sdk_.trigger();
  if (!fired) {
    return fail(fired.error());
  }
  // A stamp without its year needs the station's UTC, which a time quality
  // not yet sampled (time_utc_ns 0) does not give.
  const std::int64_t station_ns = quality_.current().time_utc_ns();
  if (!fired->stamp.year && station_ns == 0) {
    return fail(Error::kUnavailable);
  }
  const Result<UtcTime> time = utc_from_irig(fired->stamp, utc_from_ns(station_ns));
  if (!time || fired->segment != triggers_.size()) {
    return fail(Error::kMalformed);
  }
  triggers_.push_back(*time);
  return SegmentStatus{.segment = fired->segment, .trigger_time = *time, .recorded = fired->recorded};
}

Status FlirCamera::save(const std::uint32_t segment, const FrameRange& frames, const std::filesystem::path& path) {
  if (segment >= triggers_.size()) {
    return fail(Error::kOutOfRange);
  }
  const Result<std::vector<FlirFrame>> read = sdk_.read(segment, frames);
  const Result<Saved> saved = read ? to_cine(settings_, triggers_[segment], frames, *read) : fail(read.error());
  const Result<std::vector<std::byte>> bytes = saved ? write_cine(saved->cine, saved->images) : fail(saved.error());
  return bytes ? detail::write_file(path, *bytes) : fail(bytes.error());
}

}  // namespace ics::camera
