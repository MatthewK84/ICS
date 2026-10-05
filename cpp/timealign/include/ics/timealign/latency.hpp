#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "ics/common/units.hpp"

namespace ics::timealign {

// The spread of one source's link latency: the time ICS received each record
// minus its aligned valid time. A negative latency, a record received before
// its valid time, is counted like any other: it shows a clock error.
struct LatencyStats {
  std::size_t count = 0;
  Duration min{};
  // The lower median, and the 95th percentile by nearest rank.
  Duration median{};
  Duration p95{};
  Duration max{};
};

// Nothing for no latencies.
[[nodiscard]] std::optional<LatencyStats> summarize_latency(std::vector<Duration> latencies);

}  // namespace ics::timealign
