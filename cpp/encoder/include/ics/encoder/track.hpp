#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/encoder/angles.hpp"
#include "ics/encoder/pointing_model.hpp"
#include "ics/tagger/pps_clock.hpp"
#include "ics/tagger/sampler.hpp"

namespace ics::encoder {

// The longest step between two samples that a time may be interpolated in:
// a lost datagram's worth at 1,000 samples a second (ICS-036's question 2).
inline constexpr Duration kMaxGap = std::chrono::milliseconds(10);

// The samples a track holds by default: 10 s at 1,000 a second.
inline constexpr std::size_t kDefaultHistory = 10'000;

// The axis angles at one instant, interpolated between the two samples
// either side of it.
struct Interpolated {
  UtcTime utc;
  Direction encoder;
  // The axes' rates, in radians a second.
  double azimuth_rate_rad_per_s = 0.0;
  double elevation_rate_rad_per_s = 0.0;
  // The larger time bound of the two samples, and the worse clock state:
  // kHoldover if either sample was in holdover.
  Duration time_bound{};
  tagger::ClockState state = tagger::ClockState::kLocked;
  // How far the encoder angles may be from the axes' true angles at utc,
  // on the sky, in radians: time_bound times the rate on the sky, plus one
  // count of each axis. The pointing model's own error (C17) and the
  // caller's time error are not in it.
  double bound_rad = 0.0;
};

// What a track has been given.
struct TrackStats {
  // Batches added, and batches missing between their sequence numbers.
  std::uint64_t batches = 0;
  std::uint64_t lost_batches = 0;
  // Batches whose sequence number went back: repeated, reordered, or from a
  // tagger that started again. Their samples are kept if they are newer.
  std::uint64_t sequence_resets = 0;
  // Samples kept; samples dropped while the tagger's clock was acquiring;
  // and samples dropped for being no later than the newest kept.
  std::uint64_t samples = 0;
  std::uint64_t acquiring = 0;
  std::uint64_t out_of_order = 0;
};

// The newest samples' axis angles, from the tagger's batches, and the angles
// between them at any instant they span (ICS-036). The history is a fixed
// ring, allocated in make(); add() and at() allocate nothing.
class AngleTrack {
 public:
  // A track of capacity samples. kInvalidArgument for a config that is not
  // valid(), or a capacity under 2.
  [[nodiscard]] static Result<AngleTrack> make(const EncoderConfig& config,
                                               std::size_t capacity = kDefaultHistory);

  // Adds a batch's samples, dropping those without a time and those no later
  // than the newest kept, and counts the batches lost before it.
  void add(const tagger::Batch& batch) noexcept;

  // The axis angles and rates at when, linear between the samples either
  // side. kOutOfRange before the oldest sample or after the newest, and with
  // fewer than two; kUnconstrained when the samples either side are more
  // than kMaxGap apart, as across a lost batch or a clock still acquiring.
  [[nodiscard]] Result<Interpolated> at(UtcTime when) const noexcept;

  // The newest sample kept, if any.
  [[nodiscard]] std::optional<AxisAngles> newest() const noexcept;

  [[nodiscard]] std::size_t size() const noexcept { return size_; }
  [[nodiscard]] const TrackStats& stats() const noexcept { return stats_; }

 private:
  AngleTrack(const EncoderConfig& config, std::size_t capacity);

  // The index-th sample, from the oldest.
  [[nodiscard]] const AxisAngles& sample(std::size_t index) const noexcept;
  // The index of the first sample later than when: 1 to size_, for a when
  // from the oldest sample on.
  [[nodiscard]] std::size_t first_after(UtcTime when) const noexcept;
  [[nodiscard]] Result<Interpolated> between(const AxisAngles& before, const AxisAngles& after,
                                             UtcTime when) const noexcept;
  void count_sequence(std::uint32_t sequence) noexcept;
  void keep(const AxisAngles& angles) noexcept;

  EncoderConfig config_;
  // One count of each axis, in radians.
  double azimuth_count_rad_ = 0.0;
  double elevation_count_rad_ = 0.0;
  std::vector<AxisAngles> history_;
  std::size_t oldest_ = 0;
  std::size_t size_ = 0;
  std::optional<std::uint32_t> expected_sequence_;
  TrackStats stats_;
};

}  // namespace ics::encoder
