#include "emulation.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <vector>

#include "ics/camera/segment_camera.hpp"
#include "ics/camera/strobe.hpp"
#include "ics/camera/trigger_schedule.hpp"
#include "ics/common/check.hpp"
#include "ics/common/units.hpp"

namespace ics::camera::detail {
namespace {

constexpr double kNsPerSecond = 1e9;
constexpr std::uint32_t kMaxSegmentFrames = std::uint32_t{1} << 20U;
constexpr std::uint32_t kMaxSegments = 64;
constexpr std::size_t kPixelBytes = 2;
constexpr unsigned kBitsPerByte = 8;
constexpr std::uint32_t kByteMask = 0xFFU;
constexpr unsigned kKeyShift = 32;

// Standard normal draws from a counter (splitmix64 and Box-Muller), the same
// on every platform and library.
class Gaussian {
 public:
  explicit Gaussian(const std::uint64_t seed) noexcept : state_(seed) {}

  [[nodiscard]] double next() noexcept {
    const double radius = std::sqrt(-2.0 * std::log(uniform()));
    return radius * std::cos(2.0 * std::numbers::pi * uniform());
  }

 private:
  // A draw from (0, 1].
  [[nodiscard]] double uniform() noexcept {
    constexpr unsigned kMantissaShift = 11;
    constexpr double kScale = 0x1p-53;
    return static_cast<double>((bits() >> kMantissaShift) + 1) * kScale;
  }
  [[nodiscard]] std::uint64_t bits() noexcept {
    constexpr std::uint64_t kGolden = 0x9E37'79B9'7F4A'7C15U;
    constexpr std::uint64_t kMix1 = 0xBF58'476D'1CE4'E5B9U;
    constexpr std::uint64_t kMix2 = 0x94D0'49BB'1331'11EBU;
    constexpr unsigned kShift1 = 30;
    constexpr unsigned kShift2 = 27;
    constexpr unsigned kShift3 = 31;
    std::uint64_t z = (state_ += kGolden);
    z = (z ^ (z >> kShift1)) * kMix1;
    z = (z ^ (z >> kShift2)) * kMix2;
    return z ^ (z >> kShift3);
  }

  std::uint64_t state_;
};

void put_word(std::vector<std::byte>& out, const std::size_t pixel, const double value, const std::uint32_t max_value) {
  const auto word = static_cast<std::uint32_t>(std::clamp(std::round(value), 0.0, static_cast<double>(max_value)));
  out[pixel * kPixelBytes] = static_cast<std::byte>(word & kByteMask);
  out[(pixel * kPixelBytes) + 1] = static_cast<std::byte>((word >> kBitsPerByte) & kByteMask);
}

}  // namespace

Duration frames(const std::int64_t number, const std::uint32_t frame_rate) noexcept {
  // Callers check the frame rate before timing frames by it.
  static_cast<void>(check(frame_rate > 0));
  return Duration(std::llround(static_cast<double>(number) * kNsPerSecond / frame_rate));
}

bool records(const CameraSettings& settings, const TriggerSchedule& schedule) noexcept {
  const bool timing = settings.frame_rate > 0 && settings.exposure > Duration::zero() &&
                      settings.exposure < frames(1, settings.frame_rate);
  const bool segment = std::clamp(settings.segment_frames, 1U, kMaxSegmentFrames) == settings.segment_frames &&
                       settings.post_trigger_frames <= settings.segment_frames;
  return timing && segment && schedule.interval >= frames(settings.segment_frames, settings.frame_rate);
}

bool armable(const std::uint32_t segments) noexcept { return std::clamp(segments, 1U, kMaxSegments) == segments; }

bool holds(const FrameRange& held, const FrameRange& wanted) noexcept {
  const std::int64_t end = std::int64_t{wanted.first} + wanted.count;
  return wanted.count > 0 && wanted.first >= held.first && end <= std::int64_t{held.first} + held.count;
}

SegmentStatus scheduled(const CameraSettings& settings, const TriggerSchedule& schedule,
                        const std::uint32_t segment) noexcept {
  // Settings an emulated camera records keep the post-trigger frames within
  // the segment.
  static_cast<void>(check(settings.post_trigger_frames <= settings.segment_frames));
  const std::uint32_t before = settings.segment_frames - settings.post_trigger_frames;
  return SegmentStatus{.segment = segment,
                       .trigger_time = schedule.first + (schedule.interval * segment),
                       .recorded = {.first = -static_cast<std::int32_t>(before), .count = settings.segment_frames}};
}

bool fits_scene(const StrobeScene& scene, const CameraSettings& settings) noexcept {
  const Roi& r = scene.roi;
  if (r.width == 0 || r.height == 0) {
    return true;
  }
  const bool inside = std::uint64_t{r.x} + r.width <= settings.width && std::uint64_t{r.y} + r.height <= settings.height;
  const bool levels = scene.background >= 0.0 && scene.gain_per_us >= 0.0 && scene.noise_sigma >= 0.0;
  return scene.schedule.valid() && inside && levels;
}

std::vector<std::byte> render(const StrobeScene& scene, const CameraSettings& settings, const UtcTime start,
                              const std::uint32_t max_value, const std::uint64_t key) {
  const std::size_t pixels = std::size_t{settings.width} * settings.height;
  std::vector<std::byte> out(pixels * kPixelBytes);
  for (std::size_t i = 0; i < pixels; ++i) {
    put_word(out, i, scene.background, max_value);
  }
  if (scene.roi.width == 0 || scene.roi.height == 0) {
    return out;
  }
  // configure() takes only a scene whose rectangle lies within the image.
  static_cast<void>(check(fits_scene(scene, settings)));
  const std::chrono::duration<double, std::micro> light = strobe_light(scene.schedule, start, settings.exposure);
  const double lit = scene.background + (scene.gain_per_us * light.count());
  Gaussian noise(scene.seed ^ key);
  const std::uint32_t bottom = std::min(scene.roi.y + scene.roi.height, settings.height);
  const std::uint32_t right = std::min(scene.roi.x + scene.roi.width, settings.width);
  for (std::uint32_t row = scene.roi.y; row < bottom; ++row) {
    for (std::uint32_t column = scene.roi.x; column < right; ++column) {
      put_word(out, (std::size_t{row} * settings.width) + column, lit + (scene.noise_sigma * noise.next()), max_value);
    }
  }
  return out;
}

std::uint64_t frame_key(const std::uint32_t segment, const std::int32_t number) noexcept {
  return (std::uint64_t{segment} << kKeyShift) | static_cast<std::uint32_t>(number);
}

}  // namespace ics::camera::detail
