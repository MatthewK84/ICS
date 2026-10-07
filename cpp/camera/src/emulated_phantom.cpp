#include "ics/camera/emulated_phantom.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <vector>

#include "ics/camera/cine.hpp"
#include "ics/camera/phantom.hpp"
#include "ics/common/error.hpp"
#include "ics/common/units.hpp"

namespace ics::camera {
namespace {

constexpr std::uint32_t kMaxSide = 65'535;
constexpr std::uint32_t kMaxSegmentFrames = std::uint32_t{1} << 20U;
constexpr std::uint32_t kMaxSegments = 64;
constexpr double kNsPerSecond = 1e9;
// The emulated sensor: 16-bit pixels, 12 of them significant.
constexpr std::uint16_t kBitCount = 16;
constexpr std::uint32_t kRealBpp = 12;

// The time from a frame to frame number from it, at a frame rate.
[[nodiscard]] Duration frames(const std::int64_t number, const std::uint32_t frame_rate) noexcept {
  return Duration(std::llround(static_cast<double>(number) * kNsPerSecond / frame_rate));
}

[[nodiscard]] bool usable(const CameraSettings& s, const TriggerSchedule& schedule) noexcept {
  const bool size = std::clamp(s.width, 1U, kMaxSide) == s.width && std::clamp(s.height, 1U, kMaxSide) == s.height;
  const bool timing = s.frame_rate > 0 && s.exposure > Duration::zero() && s.exposure < frames(1, s.frame_rate);
  const bool segment = std::clamp(s.segment_frames, 1U, kMaxSegmentFrames) == s.segment_frames &&
                       s.post_trigger_frames <= s.segment_frames;
  return size && timing && segment && schedule.interval >= frames(s.segment_frames, s.frame_rate);
}

// A segment's frames as a cine: each frame's time is the trigger's plus its
// number of frame periods.
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

}  // namespace

Result<CameraSettings> EmulatedPhantom::configure(const CameraSettings& settings) {
  if (!usable(settings, schedule_)) {
    return fail(Error::kInvalidArgument);
  }
  settings_ = settings;
  configured_ = true;
  armed_ = 0;
  recorded_.clear();
  return settings_;
}

Status EmulatedPhantom::arm(const std::uint32_t segments) {
  if (!configured_ || std::clamp(segments, 1U, kMaxSegments) != segments) {
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
  const auto segment = static_cast<std::uint32_t>(recorded_.size());
  const std::uint32_t before = settings_.segment_frames - settings_.post_trigger_frames;
  const SegmentStatus status{.segment = segment,
                             .trigger_time = schedule_.first + (schedule_.interval * segment),
                             .recorded = {.first = -static_cast<std::int32_t>(before),
                                          .count = settings_.segment_frames}};
  recorded_.push_back(status);
  return status;
}

Status EmulatedPhantom::save(const std::uint32_t segment, const FrameRange& frames_to_save,
                             const std::filesystem::path& path) {
  const bool known = segment < recorded_.size();
  const FrameRange held = known ? recorded_[segment].recorded : FrameRange{};
  const std::int64_t end = std::int64_t{frames_to_save.first} + frames_to_save.count;
  if (!known || frames_to_save.count == 0 || frames_to_save.first < held.first ||
      end > std::int64_t{held.first} + held.count) {
    return fail(Error::kOutOfRange);
  }
  const Result<std::vector<std::byte>> bytes =
      write_cine(segment_cine(settings_, recorded_[segment].trigger_time, frames_to_save));
  if (!bytes) {
    return fail(bytes.error());
  }
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(bytes->data()), static_cast<std::streamsize>(bytes->size()));
  out.close();
  if (!out) {
    return fail(Error::kUnwritable);
  }
  return {};
}

}  // namespace ics::camera
