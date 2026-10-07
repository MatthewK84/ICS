#include "ics/strobe/detect.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "ics/camera/cine.hpp"
#include "ics/camera/image.hpp"
#include "ics/camera/strobe.hpp"
#include "ics/common/error.hpp"
#include "ics/common/units.hpp"

namespace ics::strobe {
namespace {

constexpr std::size_t kMinFrames = 3;
constexpr double kRobustSigma = 1.4826;
constexpr double kSigmas = 6.0;
constexpr double kMinLift = 1.0;

[[nodiscard]] double median(std::vector<double> values) {
  const auto middle = values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2);
  std::ranges::nth_element(values, middle);
  return *middle;
}

// Whether the flagged frames follow one another.
[[nodiscard]] bool contiguous(const std::vector<bool>& lit) {
  const auto first = std::ranges::find(lit, true);
  const auto past = std::ranges::find(first, lit.end(), false);
  return std::ranges::find(past, lit.end(), true) == lit.end();
}

[[nodiscard]] std::int64_t nearest_second(const UtcTime time) {
  return std::chrono::round<std::chrono::seconds>(time).time_since_epoch().count();
}

}  // namespace

std::vector<bool> lit_frames(const std::span<const double> brightness) {
  const double middle = median({brightness.begin(), brightness.end()});
  std::vector<double> deviations(brightness.size());
  std::ranges::transform(brightness, deviations.begin(), [middle](const double b) { return std::abs(b - middle); });
  const double threshold = middle + std::max(kSigmas * kRobustSigma * median(deviations), kMinLift);
  std::vector<bool> out(brightness.size());
  std::transform(brightness.begin(), brightness.end(), out.begin(), [threshold](const double b) { return b >= threshold; });
  return out;
}

Result<StrobeSegment> read_segment(const std::span<const std::byte> bytes, const camera::Cine& cine,
                                   const camera::Roi& roi) {
  const std::size_t count = cine.frame_times.size();
  if (count < kMinFrames) {
    return fail(Error::kEmpty);
  }
  StrobeSegment out{.second = nearest_second(cine.frame_times[count / 2]), .frames = {}, .lit = 0, .contiguous = true};
  std::vector<double> brightness;
  for (std::size_t i = 0; i < count; ++i) {
    const Result<double> mean = camera::roi_mean(bytes, cine, i, roi);
    if (!mean) {
      return fail(mean.error());
    }
    brightness.push_back(*mean);
    out.frames.push_back(StrobeFrame{.stamp = cine.frame_times[i],
                                     .exposure = i < cine.exposures.size() ? cine.exposures[i] : cine.exposure,
                                     .brightness = *mean});
  }
  const std::vector<bool> lit = lit_frames(brightness);
  out.lit = static_cast<std::size_t>(std::ranges::count(lit, true));
  out.contiguous = contiguous(lit);
  return out;
}

}  // namespace ics::strobe
