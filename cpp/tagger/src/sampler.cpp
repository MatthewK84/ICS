#include "ics/tagger/sampler.hpp"

#include <cstdint>

#include "ics/common/check.hpp"

namespace ics::tagger {

bool Sampler::add(const PpsClock& clock, const std::uint64_t ticks, const std::int32_t azimuth_counts,
                  const std::int32_t elevation_counts) noexcept {
  if (count_ == kBatchSamples) {
    count_ = 0;
    ++batch_.sequence;
  }
  // count_ starts each batch at 0 and stops at kBatchSamples.
  static_cast<void>(ics::check(count_ < kBatchSamples));
  const Tag tag = clock.tag(ticks);
  batch_.samples[count_] = Sample{.utc_ns = tag.utc_ns,
                                  .bound_ns = tag.bound_ns,
                                  .azimuth_counts = azimuth_counts,
                                  .elevation_counts = elevation_counts,
                                  .state = tag.state};
  ++count_;
  return count_ == kBatchSamples;
}

}  // namespace ics::tagger
