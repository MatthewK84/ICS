#include "ics/timealign/latency.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <vector>

#include "ics/common/check.hpp"
#include "ics/common/units.hpp"

namespace ics::timealign {
namespace {

constexpr std::size_t kPercentile = 95;
constexpr std::size_t kHundred = 100;

}  // namespace

std::optional<LatencyStats> summarize_latency(std::vector<Duration> latencies) {
  if (latencies.empty()) {
    return std::nullopt;
  }
  std::ranges::sort(latencies);
  const std::size_t count = latencies.size();
  // Nearest rank: the smallest value at or above 95 % of the values.
  const std::size_t p95_rank = ((count * kPercentile) + kHundred - 1) / kHundred;
  // The rank is 1 to count; 0 would wrap to the largest size_t.
  static_cast<void>(check(p95_rank - 1 < count));
  return LatencyStats{.count = count,
                      .min = latencies.front(),
                      .median = latencies[(count - 1) / 2],
                      .p95 = latencies[p95_rank - 1],
                      .max = latencies.back()};
}

}  // namespace ics::timealign
