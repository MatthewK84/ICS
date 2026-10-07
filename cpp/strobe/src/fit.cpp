#include "ics/strobe/fit.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

#include "ics/camera/strobe.hpp"
#include "ics/common/check.hpp"
#include "ics/common/error.hpp"
#include "ics/common/units.hpp"

namespace ics::strobe {
namespace {

constexpr Duration kCoarseStep{250};
constexpr Duration kFineStep{1};
constexpr std::size_t kMinFrames = 4;
// The fit's parameters: offset, gain and background.
constexpr double kParameters = 3.0;
constexpr double kNsPerUs = 1000.0;
// A frame whose light moves by at least this much per unit offset is partly
// lit.
constexpr double kEdgeSlope = 0.25;
// A full pulse's brightness must stand this many residual RMSs clear.
constexpr double kMinSignalToNoise = 10.0;

// The least-squares line of brightness on strobe light at one offset.
struct Line {
  double gain = 0.0;
  double background = 0.0;
  double rss = 0.0;
};

struct Best {
  Duration offset{};
  Line line;
  bool at_edge = false;
};

[[nodiscard]] double light_us(const camera::StrobeSchedule& s, const StrobeFrame& frame, const Duration offset) {
  return static_cast<double>(camera::strobe_light(s, frame.stamp - offset, frame.exposure).count()) / kNsPerUs;
}

// A gain that is not positive is no strobe at all: the line is flat.
[[nodiscard]] Line line_at(const std::span<const StrobeFrame> frames, const camera::StrobeSchedule& s,
                           const Duration offset) {
  double sx = 0.0;
  double sy = 0.0;
  double sxx = 0.0;
  double sxy = 0.0;
  double syy = 0.0;
  for (const StrobeFrame& frame : frames) {
    const double x = light_us(s, frame, offset);
    sx += x;
    sy += frame.brightness;
    sxx += x * x;
    sxy += x * frame.brightness;
    syy += frame.brightness * frame.brightness;
  }
  const auto n = static_cast<double>(frames.size());
  const double spread = sxx - (sx * sx / n);
  const double covariance = sxy - (sx * sy / n);
  const double total = std::max(syy - (sy * sy / n), 0.0);
  if (spread <= 0.0 || covariance <= 0.0) {
    return Line{.gain = 0.0, .background = sy / n, .rss = total};
  }
  const double gain = covariance / spread;
  return Line{.gain = gain, .background = (sy - (gain * sx)) / n, .rss = std::max(total - (gain * covariance), 0.0)};
}

// The offset from low to high, in steps, with the least residual.
[[nodiscard]] Best search(const std::span<const StrobeFrame> frames, const camera::StrobeSchedule& s,
                          const Duration low, const Duration high, const Duration step) {
  Best best{.offset = low, .line = line_at(frames, s, low), .at_edge = false};
  for (Duration offset = low + step; offset <= high; offset += step) {
    const Line line = line_at(frames, s, offset);
    const bool better = line.rss < best.line.rss;
    best.offset = better ? offset : best.offset;
    best.line = better ? line : best.line;
  }
  best.at_edge = best.offset == low || best.offset + step > high;
  return best;
}

// What the frames say of the offset: the frames whose light moves with it,
// and the information about it that survives fitting the gain and
// background too. With no sweep, every lit frame holds the same light, and
// a larger gain times less of the pulse fits as well as the true gain times
// all of it: no information is left.
struct Edges {
  std::size_t frames = 0;
  // In 1 / µs².
  double information = 0.0;
};

// Sums over the frames of the offset's derivative d (gain times the light's
// slope), the light x, and 1.
struct Sums {
  double dd = 0.0;
  double dx = 0.0;
  double d1 = 0.0;
  double xx = 0.0;
  double x1 = 0.0;
  double n = 0.0;
};

// The part of the derivative's sum of squares that no gain and background
// explain: the Schur complement of the 3 x 3 Fisher matrix.
[[nodiscard]] double information(const Sums& s) {
  constexpr double kRelative = 1e-9;
  const double det = (s.xx * s.n) - (s.x1 * s.x1);
  const double explained =
      det > 0.0 ? ((s.n * s.dx * s.dx) - (2.0 * s.x1 * s.dx * s.d1) + (s.xx * s.d1 * s.d1)) / det : 0.0;
  const double left = s.dd - explained;
  return left > kRelative * s.dd ? left : 0.0;
}

[[nodiscard]] Edges edges(const std::span<const StrobeFrame> frames, const camera::StrobeSchedule& s,
                          const Duration offset, const double gain) {
  constexpr double kSpanUs = 2.0 / kNsPerUs;
  Edges out;
  Sums sums;
  for (const StrobeFrame& frame : frames) {
    const double x = light_us(s, frame, offset);
    const double slope = (light_us(s, frame, offset + kFineStep) - light_us(s, frame, offset - kFineStep)) / kSpanUs;
    const double d = gain * slope;
    out.frames += std::abs(slope) >= kEdgeSlope ? 1 : 0;
    sums = Sums{.dd = sums.dd + (d * d), .dx = sums.dx + (d * x), .d1 = sums.d1 + d,
                .xx = sums.xx + (x * x), .x1 = sums.x1 + x, .n = sums.n + 1.0};
  }
  out.information = information(sums);
  return out;
}

}  // namespace

Result<OffsetFit> fit_offset(const std::span<const StrobeFrame> frames, const camera::StrobeSchedule& schedule,
                             const Duration max_offset) {
  if (frames.size() < kMinFrames) {
    return fail(Error::kEmpty);
  }
  const Best coarse = search(frames, schedule, -max_offset, max_offset, kCoarseStep);
  const Best fine = search(frames, schedule, coarse.offset - kCoarseStep, coarse.offset + kCoarseStep, kFineStep);
  const Edges lit = edges(frames, schedule, fine.offset, fine.line.gain);
  const auto n = static_cast<double>(frames.size());
  const double rms = std::sqrt(fine.line.rss / n);
  const double pulse = fine.line.gain * static_cast<double>(schedule.pulse_width.count()) / kNsPerUs;
  if (coarse.at_edge || lit.information <= 0.0 || pulse < kMinSignalToNoise * rms) {
    return fail(Error::kUnconstrained);
  }
  const double variance = fine.line.rss / (n - kParameters);
  static_cast<void>(check(variance >= 0.0));
  // Information needs a positive gain and a partly lit frame.
  static_cast<void>(check(fine.line.gain > 0.0));
  static_cast<void>(check(lit.frames > 0));
  const double sigma_ns = std::ceil(std::sqrt(variance / lit.information) * kNsPerUs);
  return OffsetFit{.offset = fine.offset,
                   .sigma = Duration(std::max(static_cast<std::int64_t>(sigma_ns), std::int64_t{1})),
                   .gain_per_us = fine.line.gain,
                   .background = fine.line.background,
                   .rms = rms,
                   .frames = frames.size(),
                   .edge_frames = lit.frames};
}

}  // namespace ics::strobe
