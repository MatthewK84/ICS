#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "ics/tagger/pps_clock.hpp"

namespace ics::tagger {

// The samples in one batch, and so in one datagram: 10 ms at 1,000 samples a
// second.
inline constexpr std::size_t kBatchSamples = 10;

// Both mount axes' encoder counts, latched at one instant, with its time.
struct Sample {
  // UTC of the latch, in nanoseconds since the Unix epoch; 0 while acquiring.
  std::int64_t utc_ns = 0;
  // The most utc_ns may be off, in ns; 0 while acquiring.
  std::int64_t bound_ns = 0;
  // The axes' encoder counts, as the hardware decoders hold them.
  std::int32_t azimuth_counts = 0;
  std::int32_t elevation_counts = 0;
  ClockState state = ClockState::kAcquiring;
};

// kBatchSamples samples in the order latched, numbered so that the host can
// count the batches lost.
struct Batch {
  std::uint32_t sequence = 0;
  std::array<Sample, kBatchSamples> samples{};
};

// Stamps each latched sample with the clock and gathers them into batches
// (ICS-032). The board calls add on its sample tick with the counter and both
// encoder counts it latched together. Nothing allocates.
class Sampler {
 public:
  // Adds a sample latched at ticks; true when it fills the batch, which then
  // stays readable until the next add starts the next batch.
  [[nodiscard]] bool add(const PpsClock& clock, std::uint64_t ticks, std::int32_t azimuth_counts,
                         std::int32_t elevation_counts) noexcept;

  // The batch being filled, or the one just filled.
  [[nodiscard]] const Batch& batch() const noexcept { return batch_; }

 private:
  Batch batch_;
  std::size_t count_ = 0;
};

}  // namespace ics::tagger
