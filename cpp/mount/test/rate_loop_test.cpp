#include "ics/mount/rate_loop.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
#include <thread>

#include <gtest/gtest.h>

#include "emulated_pwi4.hpp"
#include "mount_support.hpp"

namespace {

using ics::mount::Fault;
using ics::mount::LoopSettings;
using ics::mount::RateDemand;
using ics::mount::RateLoop;
using ics::mount::testing::EmulatedPwi4;
using ics::mount::testing::FixedSource;
using ics::mount::testing::fresh;
using std::chrono::milliseconds;

// Runs the loop until stop_after has passed, or it stops by itself.
ics::Status run_for(RateLoop& loop, const milliseconds stop_after) {
  std::atomic<bool> stop{false};
  std::thread stopper([&stop, stop_after] {
    std::this_thread::sleep_for(stop_after);
    stop = true;
  });
  const ics::Status status = loop.run(stop);
  stopper.join();
  return status;
}

// A source whose demand is always made now.
class Fresh final : public ics::mount::RateSource {
 public:
  Fresh(const double az_deg_s, const double alt_deg_s) : az_(az_deg_s), alt_(alt_deg_s) {}
  [[nodiscard]] std::optional<RateDemand> latest() override { return fresh(az_, alt_); }

 private:
  double az_;
  double alt_;
};

TEST(RateLoop, RefusesARateOutsideTwentyToFiftyHertz) {
  EmulatedPwi4 pwi4;
  auto client = ics::mount::testing::client_for(pwi4);
  Fresh source(1, 1);
  for (const double hz : {10.0, 60.0}) {
    RateLoop loop(client, source, LoopSettings{.rate_hz = hz});
    std::atomic<bool> stop{false};
    EXPECT_EQ(loop.run(stop).error(), ics::Error::kInvalidArgument);
  }
  EXPECT_EQ(pwi4.seen().requests, 0U);
}

TEST(RateLoop, SendsTheDemandUntilStoppedThenZeroRates) {
  EmulatedPwi4 pwi4;
  auto client = ics::mount::testing::client_for(pwi4);
  Fresh source(2, -1);
  RateLoop loop(client, source, LoopSettings{.rate_hz = 50.0});
  ASSERT_TRUE(run_for(loop, milliseconds(300)).has_value());
  EXPECT_GE(loop.stats().periods, 10U);
  EXPECT_EQ(loop.stats().fault, Fault::kNone);
  EXPECT_EQ(pwi4.seen().rate_commands, loop.stats().periods);
  EXPECT_EQ(pwi4.seen().stop_rates, 1U);
  EXPECT_EQ(pwi4.seen().stops, 0U);
}

TEST(RateLoop, CountsWhatTheLimitsChanged) {
  // Altitude at 12°: down at the 10°/s maximum would pass the 5° limit within
  // a second.
  auto axes = ics::mount::testing::default_axes();
  axes[1].position_deg = 12.0;
  EmulatedPwi4 pwi4(axes);
  auto client = ics::mount::testing::client_for(pwi4);
  // Too fast in azimuth, and down.
  Fresh source(25, -30);
  RateLoop loop(client, source, LoopSettings{.rate_hz = 50.0});
  ASSERT_TRUE(run_for(loop, milliseconds(200)).has_value());
  EXPECT_GE(loop.stats().clamped, loop.stats().periods);
  EXPECT_GE(loop.stats().stopped_at_limit, 1U);
  EXPECT_LE(pwi4.seen().max_abs_rate_deg_s[0], 10.0 + 1e-9);
}

TEST(RateLoop, StopsTheMountOnAStaleDemand) {
  EmulatedPwi4 pwi4;
  auto client = ics::mount::testing::client_for(pwi4);
  RateDemand old = fresh(1, 1);
  old.time -= std::chrono::seconds(1);
  FixedSource source(old);
  RateLoop loop(client, source, LoopSettings{.rate_hz = 20.0});
  EXPECT_EQ(run_for(loop, milliseconds(500)).error(), ics::Error::kUnavailable);
  EXPECT_EQ(loop.stats().fault, Fault::kStaleDemand);
  EXPECT_EQ(loop.stats().periods, 1U);
  EXPECT_EQ(pwi4.seen().rate_commands, 0U);
  EXPECT_EQ(pwi4.seen().stop_rates, 1U);
  EXPECT_EQ(pwi4.seen().stops, 1U);
}

TEST(RateLoop, StopsTheMountWithoutADemand) {
  EmulatedPwi4 pwi4;
  auto client = ics::mount::testing::client_for(pwi4);
  // Owned through the interface, as ics-mountd will own its source.
  const std::unique_ptr<ics::mount::RateSource> source = std::make_unique<FixedSource>(std::nullopt);
  RateLoop loop(client, *source, LoopSettings{.rate_hz = 20.0});
  EXPECT_EQ(run_for(loop, milliseconds(500)).error(), ics::Error::kUnavailable);
  EXPECT_EQ(loop.stats().fault, Fault::kStaleDemand);
}

TEST(RateLoop, RidesOutAFailedPeriod) {
  EmulatedPwi4 pwi4;
  auto client = ics::mount::testing::client_for(pwi4);
  Fresh source(1, 1);
  RateLoop loop(client, source, LoopSettings{.rate_hz = 50.0});
  pwi4.fail_next(4);
  ASSERT_TRUE(run_for(loop, milliseconds(300)).has_value());
  EXPECT_GE(loop.stats().failed_periods, 1U);
  EXPECT_EQ(loop.stats().fault, Fault::kNone);
}

TEST(RateLoop, StopsTheMountWhenTheLinkFails) {
  EmulatedPwi4 pwi4;
  auto client = ics::mount::testing::client_for(pwi4);
  Fresh source(1, 1);
  RateLoop loop(client, source, LoopSettings{.rate_hz = 50.0});
  // Every answer comes after the period's end.
  pwi4.delay(milliseconds(30));
  EXPECT_EQ(run_for(loop, milliseconds(2000)).error(), ics::Error::kUnavailable);
  EXPECT_EQ(loop.stats().fault, Fault::kLink);
  EXPECT_EQ(loop.stats().failed_periods, 3U);
}

// A source as slow as a whole period makes each period late, and leaves no
// time to reach the mount.
class Slow final : public ics::mount::RateSource {
 public:
  [[nodiscard]] std::optional<RateDemand> latest() override {
    std::this_thread::sleep_for(milliseconds(25));
    return fresh(1, 1);
  }
};

TEST(RateLoop, CountsLatePeriods) {
  EmulatedPwi4 pwi4;
  auto client = ics::mount::testing::client_for(pwi4);
  Slow source;
  RateLoop loop(client, source, LoopSettings{.rate_hz = 50.0});
  EXPECT_EQ(run_for(loop, milliseconds(1000)).error(), ics::Error::kUnavailable);
  // The third failed period stops the loop before it is counted as late.
  EXPECT_EQ(loop.stats().late_periods, 2U);
  EXPECT_EQ(loop.stats().fault, Fault::kLink);
}

}  // namespace
