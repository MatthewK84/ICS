#include "ics/encoder/track.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <optional>

#include "ics/common/check.hpp"
#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/encoder/angles.hpp"
#include "ics/encoder/pointing_model.hpp"
#include "ics/tagger/pps_clock.hpp"
#include "ics/tagger/sampler.hpp"

namespace ics::encoder {
namespace {

// A step in sequence numbers of this much or more, modulo 2^32, went back.
constexpr std::uint32_t kBackwards = 1U << 31U;
constexpr double kSecondsPerNs = 1e-9;

[[nodiscard]] double count_rad(const AxisConfig& axis) noexcept {
  return 2.0 * std::numbers::pi / static_cast<double>(axis.counts_per_rev);
}

[[nodiscard]] double seconds(const Duration duration) noexcept {
  return static_cast<double>(duration.count()) * kSecondsPerNs;
}

}  // namespace

AngleTrack::AngleTrack(const EncoderConfig& config, const std::size_t capacity)
    : config_(config),
      azimuth_count_rad_(count_rad(config.azimuth)),
      elevation_count_rad_(count_rad(config.elevation)),
      history_(capacity) {}

Result<AngleTrack> AngleTrack::make(const EncoderConfig& config, const std::size_t capacity) {
  if (!valid(config) || capacity < 2) {
    return fail(Error::kInvalidArgument);
  }
  return AngleTrack(config, capacity);
}

void AngleTrack::add(const tagger::Batch& batch) noexcept {
  count_sequence(batch.sequence);
  for (const tagger::Sample& raw : batch.samples) {
    // The config was checked in make(), so only an acquiring sample fails.
    const Result<AxisAngles> angles = angles_from(raw, config_);
    if (angles) {
      keep(*angles);
    } else {
      ++stats_.acquiring;
    }
  }
}

void AngleTrack::count_sequence(const std::uint32_t sequence) noexcept {
  ++stats_.batches;
  if (expected_sequence_) {
    const std::uint32_t skipped = sequence - *expected_sequence_;
    if (skipped < kBackwards) {
      stats_.lost_batches += skipped;
    } else {
      ++stats_.sequence_resets;
    }
  }
  expected_sequence_ = sequence + 1U;
}

void AngleTrack::keep(const AxisAngles& angles) noexcept {
  if (size_ > 0 && angles.utc <= sample(size_ - 1).utc) {
    ++stats_.out_of_order;
    return;
  }
  const std::size_t capacity = history_.size();
  static_cast<void>(check(capacity >= 2));
  static_cast<void>(check(size_ <= capacity));
  history_[(oldest_ + size_) % capacity] = angles;
  if (size_ < capacity) {
    ++size_;
  } else {
    oldest_ = (oldest_ + 1) % capacity;
  }
  ++stats_.samples;
}

const AxisAngles& AngleTrack::sample(const std::size_t index) const noexcept {
  static_cast<void>(check(index < size_));
  return history_[(oldest_ + index) % history_.size()];
}

std::optional<AxisAngles> AngleTrack::newest() const noexcept {
  if (size_ == 0) {
    return std::nullopt;
  }
  return sample(size_ - 1);
}

std::size_t AngleTrack::first_after(const UtcTime when) const noexcept {
  // sample(low) is at or before when; the answer is in (low, high].
  std::size_t low = 0;
  std::size_t high = size_;
  while (high - low > 1) {
    const std::size_t middle = low + ((high - low) / 2);
    if (sample(middle).utc <= when) {
      low = middle;
    } else {
      high = middle;
    }
  }
  return high;
}

Result<Interpolated> AngleTrack::at(const UtcTime when) const noexcept {
  if (size_ < 2 || when < sample(0).utc || when > sample(size_ - 1).utc) {
    return fail(Error::kOutOfRange);
  }
  // At the newest sample itself, the last two samples bracket it.
  const std::size_t after = std::min(first_after(when), size_ - 1);
  return between(sample(after - 1), sample(after), when);
}

Result<Interpolated> AngleTrack::between(const AxisAngles& before, const AxisAngles& after,
                                         const UtcTime when) const noexcept {
  const Duration step = after.utc - before.utc;
  if (step > kMaxGap) {
    return fail(Error::kUnconstrained);
  }
  // keep() holds only samples later than the one before.
  static_cast<void>(check(step > Duration::zero()));
  static_cast<void>(check(when >= before.utc));
  static_cast<void>(check(when <= after.utc));
  const double fraction = static_cast<double>((when - before.utc).count()) / static_cast<double>(step.count());
  const double d_azimuth = wrap_pi(after.encoder.azimuth_rad - before.encoder.azimuth_rad);
  const double d_elevation = after.encoder.elevation_rad - before.encoder.elevation_rad;
  Interpolated out{
      .utc = when,
      .encoder = Direction{.azimuth_rad = wrap_turn(before.encoder.azimuth_rad + (fraction * d_azimuth)),
                           .elevation_rad = before.encoder.elevation_rad + (fraction * d_elevation)},
      .azimuth_rate_rad_per_s = d_azimuth / seconds(step),
      .elevation_rate_rad_per_s = d_elevation / seconds(step),
      .time_bound = std::max(before.bound, after.bound),
      // kHoldover is the larger value, and kAcquiring samples are not kept.
      .state = std::max(before.state, after.state),
      .bound_rad = 0.0};
  const double cos_e = std::cos(out.encoder.elevation_rad);
  const double sky_rate = std::hypot(out.azimuth_rate_rad_per_s * cos_e, out.elevation_rate_rad_per_s);
  out.bound_rad = (seconds(out.time_bound) * sky_rate) + std::hypot(azimuth_count_rad_ * cos_e, elevation_count_rad_);
  return out;
}

}  // namespace ics::encoder
