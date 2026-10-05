#include "ics/timealign/clock_fit.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <span>
#include <vector>

#include "ics/common/check.hpp"
#include "ics/common/error.hpp"
#include "ics/common/units.hpp"

namespace ics::timealign {
namespace {

constexpr std::int64_t kNsPerUs = 1'000;
constexpr double kNsPerUsDouble = 1'000.0;
// A normal distribution's standard deviation over its median absolute
// deviation.
constexpr double kMadToSigma = 1.4826;

// A sample relative to the sortie's first: its boot time since, in
// microseconds, and how far its offset (UTC minus boot time) has moved since,
// in nanoseconds.
struct Point {
  double x = 0.0;
  double y = 0.0;
};

// A straight line through a centroid.
struct Line {
  double x0 = 0.0;
  double y0 = 0.0;
  double slope = 0.0;

  [[nodiscard]] double at(const double x) const noexcept { return y0 + (slope * (x - x0)); }
};

[[nodiscard]] bool in_range(const ClockSample& sample) noexcept {
  return sample.boot_us >= 0 && sample.boot_us <= kMaxBootUs && sample.utc_ns >= 0 && sample.utc_ns <= kMaxUtcNs;
}

// UTC minus boot time, in nanoseconds: within +-2100 years for a sample in
// range, so the difference of two always fits.
[[nodiscard]] std::int64_t offset_ns(const ClockSample& sample) noexcept {
  return sample.utc_ns - (sample.boot_us * kNsPerUs);
}

[[nodiscard]] std::vector<Point> points(const std::span<const ClockSample> samples) {
  const ClockSample& first = samples.front();
  std::vector<Point> out;
  out.reserve(samples.size());
  std::ranges::transform(samples, std::back_inserter(out), [&first](const ClockSample& sample) {
    return Point{.x = static_cast<double>(sample.boot_us - first.boot_us),
                 .y = static_cast<double>(offset_ns(sample) - offset_ns(first))};
  });
  return out;
}

// The least-squares line through the used points; nothing when their x are
// all the same. The caller ensures at least two are used.
[[nodiscard]] std::optional<Line> least_squares(const std::vector<Point>& points, const std::vector<bool>& used) {
  double count = 0.0;
  double sum_x = 0.0;
  double sum_y = 0.0;
  for (std::size_t i = 0; i < points.size(); ++i) {
    count += used[i] ? 1.0 : 0.0;
    sum_x += used[i] ? points[i].x : 0.0;
    sum_y += used[i] ? points[i].y : 0.0;
  }
  static_cast<void>(check(count >= 2.0));
  const Line centroid{.x0 = sum_x / count, .y0 = sum_y / count, .slope = 0.0};
  double sxx = 0.0;
  double sxy = 0.0;
  for (std::size_t i = 0; i < points.size(); ++i) {
    const double dx = used[i] ? points[i].x - centroid.x0 : 0.0;
    sxx += dx * dx;
    sxy += dx * (points[i].y - centroid.y0);
  }
  if (!(sxx > 0.0)) {
    return std::nullopt;
  }
  return Line{.x0 = centroid.x0, .y0 = centroid.y0, .slope = sxy / sxx};
}

// Leaves out the used points whose residual is an outlier; how many it left
// out.
[[nodiscard]] std::size_t reject(const std::vector<Point>& points, const Line& line, std::vector<bool>& used,
                                 const FitSettings& settings) {
  std::vector<double> residuals;
  for (std::size_t i = 0; i < points.size(); ++i) {
    if (used[i]) {
      residuals.push_back(std::abs(points[i].y - line.at(points[i].x)));
    }
  }
  // fit_clock rejects only while at least two points are used.
  static_cast<void>(check(!residuals.empty()));
  const auto middle = residuals.begin() + static_cast<std::ptrdiff_t>(residuals.size() / 2);
  std::ranges::nth_element(residuals, middle);
  const double limit =
      std::max(settings.reject_mads * kMadToSigma * *middle, Nanoseconds(settings.reject_floor).count());
  std::size_t count = 0;
  for (std::size_t i = 0; i < points.size(); ++i) {
    const bool outlier = used[i] && std::abs(points[i].y - line.at(points[i].x)) > limit;
    used[i] = used[i] && !outlier;
    count += outlier ? 1U : 0U;
  }
  return count;
}

// The fit's residuals and boot time span, over the used samples.
void describe(ClockFit& fit, const std::span<const ClockSample> samples, const std::vector<Point>& points,
              const std::vector<bool>& used, const Line& line) {
  double squares = 0.0;
  bool first = true;
  for (std::size_t i = 0; i < samples.size(); ++i) {
    if (!used[i]) {
      continue;
    }
    const double residual = points[i].y - line.at(points[i].x);
    squares += residual * residual;
    fit.residual_max = std::max(fit.residual_max, Nanoseconds(std::abs(residual)));
    fit.first_boot_us = first ? samples[i].boot_us : std::min(fit.first_boot_us, samples[i].boot_us);
    fit.last_boot_us = first ? samples[i].boot_us : std::max(fit.last_boot_us, samples[i].boot_us);
    first = false;
  }
  static_cast<void>(check(fit.used > 0));
  fit.residual_rms = Nanoseconds(std::sqrt(squares / static_cast<double>(fit.used)));
}

// The model through the used points' centroid, at a whole microsecond.
[[nodiscard]] Result<ClockFit> make_fit(const std::span<const ClockSample> samples, const std::vector<Point>& points,
                                        const std::vector<bool>& used, const Line& line) {
  const ClockSample& first = samples.front();
  const double x0 = std::round(line.x0);
  // The centroid lies among the used samples' boot times, all in range.
  const std::int64_t origin_boot_us = first.boot_us + static_cast<std::int64_t>(x0);
  static_cast<void>(check(std::clamp(origin_boot_us, std::int64_t{0}, kMaxBootUs) == origin_boot_us));
  const std::int64_t base_ns = (origin_boot_us * kNsPerUs) + offset_ns(first);
  const double delta_ns = line.at(x0);
  const double origin_ns = static_cast<double>(base_ns) + delta_ns;
  if (!(origin_ns >= 0.0 && origin_ns <= static_cast<double>(kMaxUtcNs))) {
    return fail(Error::kInvalidArgument);
  }
  const std::size_t used_count = static_cast<std::size_t>(std::ranges::count(used, true));
  ClockFit fit{.model = ClockModel(origin_boot_us, base_ns + std::llround(delta_ns), line.slope / kNsPerUsDouble),
               .used = used_count,
               .rejected = samples.size() - used_count};
  describe(fit, samples, points, used, line);
  return fit;
}

}  // namespace

std::optional<UtcTime> ClockModel::utc(const std::int64_t boot_us) const noexcept {
  const bool boot_in_range = boot_us >= 0 && boot_us <= kMaxBootUs;
  const bool origin_in_range = origin_boot_us_ >= 0 && origin_boot_us_ <= kMaxBootUs && origin_utc_ns_ >= 0 &&
                               origin_utc_ns_ <= kMaxUtcNs;
  if (!boot_in_range || !origin_in_range) {
    return std::nullopt;
  }
  const std::int64_t elapsed_us = boot_us - origin_boot_us_;
  const double correction_ns = static_cast<double>(elapsed_us) * kNsPerUsDouble * drift_;
  const double utc_ns =
      static_cast<double>(origin_utc_ns_) + (static_cast<double>(elapsed_us) * kNsPerUsDouble) + correction_ns;
  if (!(utc_ns >= 0.0 && utc_ns <= static_cast<double>(kMaxUtcNs))) {
    return std::nullopt;
  }
  return utc_from_ns(origin_utc_ns_ + (elapsed_us * kNsPerUs) + std::llround(correction_ns));
}

Result<ClockFit> fit_clock(const std::span<const ClockSample> samples, const FitSettings& settings) {
  if (!std::ranges::all_of(samples, in_range)) {
    return fail(Error::kInvalidArgument);
  }
  const std::size_t minimum = std::max<std::size_t>(settings.min_samples, 2);
  if (samples.size() < minimum) {
    return fail(Error::kEmpty);
  }
  const std::vector<Point> all = points(samples);
  std::vector<bool> used(samples.size(), true);
  std::optional<Line> line = least_squares(all, used);
  for (std::size_t round = 0; line && round < settings.max_iterations; ++round) {
    if (reject(all, *line, used, settings) == 0) {
      break;
    }
    const bool enough = static_cast<std::size_t>(std::ranges::count(used, true)) >= minimum;
    line = enough ? least_squares(all, used) : std::nullopt;
  }
  if (!line) {
    return fail(Error::kEmpty);
  }
  return make_fit(samples, all, used, *line);
}

}  // namespace ics::timealign
