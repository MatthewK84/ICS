#include "ics/camera/emulated_phantom.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

#include "emulation.hpp"
#include "file_out.hpp"
#include "ics/camera/cine.hpp"
#include "ics/camera/segment_camera.hpp"
#include "ics/camera/strobe.hpp"
#include "ics/camera/trigger_schedule.hpp"
#include "ics/common/error.hpp"
#include "ics/common/units.hpp"

namespace ics::camera {
namespace {

using detail::frames;

constexpr std::uint32_t kMaxSide = 65'535;
// The emulated sensor: 16-bit pixels, 12 of them significant.
constexpr std::uint16_t kBitCount = 16;
constexpr std::uint32_t kRealBpp = 12;
constexpr std::uint32_t kMaxValue = (std::uint32_t{1} << kRealBpp) - 1;
constexpr std::size_t kPixelBytes = 2;
// The largest cine write_cine writes.
constexpr std::uint64_t kMaxCineBytes = std::uint64_t{1} << 30U;

[[nodiscard]] bool usable(const CameraSettings& s, const TriggerSchedule& schedule, const StrobeScene& scene) noexcept {
  const bool size = std::clamp(s.width, 1U, kMaxSide) == s.width && std::clamp(s.height, 1U, kMaxSide) == s.height;
  return size && detail::records(s, schedule) && detail::fits_scene(scene, s);
}

// A segment's frames as a cine, stamped from its trigger's stamp: each
// frame's time is the trigger's plus its number of frame periods.
[[nodiscard]] Cine segment_cine(const CameraSettings& settings, const UtcTime trigger, const FrameRange& range) {
  Cine cine{.first_image_no = range.first,
            .trigger_time = trigger,
            .width = settings.width,
            .height = settings.height,
            .bit_count = kBitCount,
            .real_bpp = kRealBpp,
            .frame_rate = settings.frame_rate,
            .exposure = settings.exposure,
            .frame_times = {},
            .exposures = std::vector<Duration>(range.count, settings.exposure),
            .image_offsets = {}};
  cine.frame_times.reserve(range.count);
  for (std::uint32_t i = 0; i < range.count; ++i) {
    cine.frame_times.push_back(trigger + frames(std::int64_t{range.first} + i, settings.frame_rate));
  }
  return cine;
}

// The images of a segment's frames, each lit as its true exposure is; none
// for images no cine could hold, which write_cine then refuses.
[[nodiscard]] std::vector<std::byte> segment_images(const StrobeScene& scene, const CameraSettings& settings,
                                                    const SegmentStatus& segment, const Cine& cine) {
  std::vector<std::byte> images;
  const std::uint64_t image_bytes = std::uint64_t{settings.width} * settings.height * kPixelBytes;
  if (image_bytes * cine.frame_times.size() > kMaxCineBytes) {
    return images;
  }
  for (std::size_t i = 0; i < cine.frame_times.size(); ++i) {
    const std::int32_t number = cine.first_image_no + static_cast<std::int32_t>(i);
    const std::vector<std::byte> image = detail::render(scene, settings, cine.frame_times[i] - scene.stamp_offset,
                                                        kMaxValue, detail::frame_key(segment.segment, number));
    images.insert(images.end(), image.begin(), image.end());
  }
  return images;
}

}  // namespace

Result<CameraSettings> EmulatedPhantom::configure(const CameraSettings& settings) {
  if (!usable(settings, schedule_, scene_)) {
    return fail(Error::kInvalidArgument);
  }
  settings_ = settings;
  settings_.window_x = 0;
  settings_.window_y = 0;
  configured_ = true;
  armed_ = 0;
  recorded_.clear();
  return settings_;
}

Status EmulatedPhantom::arm(const std::uint32_t segments) {
  if (!configured_ || !detail::armable(segments)) {
    return fail(Error::kInvalidArgument);
  }
  armed_ = segments;
  recorded_.clear();
  return {};
}

Result<SegmentStatus> EmulatedPhantom::trigger() {
  if (armed_ == 0) {
    return fail(Error::kInvalidArgument);
  }
  if (recorded_.size() >= armed_) {
    return fail(Error::kFull);
  }
  SegmentStatus status = detail::scheduled(settings_, schedule_, static_cast<std::uint32_t>(recorded_.size()));
  status.trigger_time += scene_.stamp_offset;
  recorded_.push_back(status);
  return status;
}

Status EmulatedPhantom::save(const std::uint32_t segment, const FrameRange& frames_to_save,
                             const std::filesystem::path& path) {
  if (segment >= recorded_.size() || !detail::holds(recorded_[segment].recorded, frames_to_save)) {
    return fail(Error::kOutOfRange);
  }
  const Cine cine = segment_cine(settings_, recorded_[segment].trigger_time, frames_to_save);
  const Result<std::vector<std::byte>> bytes =
      write_cine(cine, segment_images(scene_, settings_, recorded_[segment], cine));
  return bytes ? detail::write_file(path, *bytes) : fail(bytes.error());
}

}  // namespace ics::camera
