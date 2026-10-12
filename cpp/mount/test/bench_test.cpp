// The mount client's bench (ICS-033): the rate loop drives the emulated PWI4
// at 20 Hz and at 50 Hz with a moving demand, as the tracker will.
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <thread>
#include <utility>

#include <gtest/gtest.h>

#include "emulated_pwi4.hpp"
#include "ics/mount/rate_loop.hpp"
#include "mount_support.hpp"

namespace {

using ics::mount::Fault;
using ics::mount::LoopSettings;
using ics::mount::LoopStats;
using ics::mount::RateLoop;
using ics::mount::testing::EmulatedPwi4;
using ics::mount::testing::Seen;
using ics::mount::testing::SweepSource;

constexpr std::chrono::seconds kRun{10};

struct Bench {
  LoopStats stats;
  Seen seen;
  bool stopped_cleanly = false;
};

// Runs the loop at rate_hz for kRun, against an emulated PWI4 that takes 1 ms
// to answer.
Bench run_bench(const double rate_hz) {
  EmulatedPwi4 pwi4;
  pwi4.delay(std::chrono::milliseconds(1));
  // Each attempt may take 40% of a period, which leaves room for a retry.
  ics::mount::Pwi4Settings settings = ics::mount::testing::settings_for(pwi4);
  settings.timeout = std::chrono::milliseconds(std::llround(400.0 / rate_hz));
  auto client = std::move(ics::mount::Pwi4Client::make(settings).value());
  SweepSource source;
  RateLoop loop(client, source, LoopSettings{.rate_hz = rate_hz});
  std::atomic<bool> stop{false};
  std::thread stopper([&stop] {
    std::this_thread::sleep_for(kRun);
    stop = true;
  });
  const bool stopped_cleanly = loop.run(stop).has_value();
  stopper.join();
  return {.stats = loop.stats(), .seen = pwi4.seen(), .stopped_cleanly = stopped_cleanly};
}

// ICS-033's "Done when": a stable 20 to 50 Hz rate loop, here against the
// emulated PWI4: a rate command each period, with at most 1% of periods late
// or missing theirs, no fault, every command within the limits, and the axes
// following the demand. A shared machine stalls now and then, and a stall
// past the timeout costs that period's command, which the loop rides out.
void expect_stable(const Bench& bench, const double rate_hz) {
  const auto expected = static_cast<std::uint64_t>(rate_hz * static_cast<double>(kRun.count()));
  EXPECT_TRUE(bench.stopped_cleanly);
  EXPECT_EQ(bench.stats.fault, Fault::kNone);
  EXPECT_GE(bench.stats.periods, expected * 98 / 100);
  EXPECT_LE(bench.stats.periods, expected + 1);
  EXPECT_LE((bench.stats.late_periods + bench.stats.failed_periods) * 100, bench.stats.periods);
  // Every period that reached the mount sent its command; a retry can send
  // one twice, which is safe, since setting a rate is idempotent.
  EXPECT_GE(bench.seen.rate_commands, bench.stats.periods - bench.stats.failed_periods);
  EXPECT_EQ(bench.stats.clamped, 0U);
  // Within 10°/s, and the axes within their limits throughout.
  EXPECT_LE(bench.seen.max_abs_rate_deg_s[0], 10.0);
  EXPECT_LE(bench.seen.max_abs_rate_deg_s[1], 10.0);
  EXPECT_GE(bench.seen.min_position_deg[1], 5.0);
  EXPECT_LE(bench.seen.max_position_deg[1], 85.0);
  // The axes kept up: on average within 0.1°/s of the rate commanded, which
  // the emulator's 30°/s² reaches within a period.
  const auto samples = static_cast<double>(bench.seen.rate_error_samples);
  EXPECT_LT(bench.seen.rate_error_sum_deg_s[0] / samples, 0.1);
  EXPECT_LT(bench.seen.rate_error_sum_deg_s[1] / samples, 0.1);
  testing::Test::RecordProperty("periods", static_cast<int>(bench.stats.periods));
  testing::Test::RecordProperty("late_periods", static_cast<int>(bench.stats.late_periods));
  testing::Test::RecordProperty("failed_periods", static_cast<int>(bench.stats.failed_periods));
}

TEST(MountBench, HoldsAStableRateLoopAtFiftyHertz) { expect_stable(run_bench(50.0), 50.0); }

TEST(MountBench, HoldsAStableRateLoopAtTwentyHertz) { expect_stable(run_bench(20.0), 20.0); }

}  // namespace
