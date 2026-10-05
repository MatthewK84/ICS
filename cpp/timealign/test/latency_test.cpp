#include "ics/timealign/latency.hpp"

#include <chrono>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include "ics/common/units.hpp"

namespace ics::timealign {
namespace {

using std::chrono::milliseconds;

TEST(Latency, SummarizesTheSpread) {
  std::vector<Duration> latencies;
  for (int ms = 20; ms >= -1; --ms) {
    latencies.emplace_back(milliseconds(ms));
  }
  const std::optional<LatencyStats> stats = summarize_latency(latencies);
  ASSERT_TRUE(stats.has_value());
  EXPECT_EQ(stats->count, 22U);
  EXPECT_EQ(stats->min, milliseconds(-1));
  EXPECT_EQ(stats->median, milliseconds(9));
  EXPECT_EQ(stats->p95, milliseconds(19));
  EXPECT_EQ(stats->max, milliseconds(20));
}

TEST(Latency, HasNothingToSummarizeForNoRecords) {
  EXPECT_EQ(summarize_latency({}), std::nullopt);
  const std::optional<LatencyStats> one = summarize_latency({milliseconds(3)});
  ASSERT_TRUE(one.has_value());
  EXPECT_EQ(one->median, milliseconds(3));
  EXPECT_EQ(one->p95, milliseconds(3));
}

}  // namespace
}  // namespace ics::timealign
