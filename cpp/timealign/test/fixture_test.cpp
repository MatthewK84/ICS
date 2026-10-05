// ICS-026's check against real SITL clocks. fixtures/crossing-clocks.tsv holds
// the SYSTEM_TIME clock pairs and GLOBAL_POSITION_INT boot times of a whole
// crossing engagement the SITL rig (ICS-018) flew with PX4 v1.17.0 (system 1)
// and ArduCopter 4.7.1 (system 2), as the rig's own decoder read them from the
// run's TAP capture. See fixtures/README.md.
#include <chrono>
#include <cstdint>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/timealign/check.hpp"
#include "ics/timealign/clock_fit.hpp"

namespace ics::timealign {
namespace {

using std::chrono::microseconds;
using std::chrono::seconds;

constexpr unsigned kPx4 = 1;
constexpr unsigned kArduCopter = 2;
constexpr std::int64_t kThousand = 1'000;

struct Vehicle {
  std::vector<ClockSample> samples;
  std::vector<std::int64_t> positions;
};

using Vehicles = std::map<unsigned, Vehicle>;

Vehicles read_crossing() {
  std::ifstream file(ICS_TIMEALIGN_FIXTURES "/crossing-clocks.tsv");
  Vehicles out;
  std::string line;
  while (std::getline(file, line)) {
    std::istringstream fields(line);
    std::string kind;
    unsigned system = 0;
    std::int64_t boot_ms = 0;
    std::int64_t unix_us = 0;
    fields >> kind >> system >> boot_ms;
    if (kind == "clock") {
      fields >> unix_us;
      out[system].samples.push_back({.boot_us = boot_ms * kThousand, .utc_ns = unix_us * kThousand});
    } else {
      out[system].positions.push_back(boot_ms * kThousand);
    }
  }
  return out;
}

// Each vehicle's clock pairs and position boot times, by MAVLink system.
const Vehicles& crossing() {
  static const Vehicles vehicles = read_crossing();
  return vehicles;
}

const Withholding kOutage{.from = 0.2, .to = 0.8};

TEST(CrossingClocks, HoldTheWholeEngagement) {
  ASSERT_EQ(crossing().size(), 2U);
  EXPECT_EQ(crossing().at(kPx4).samples.size(), 150U);
  EXPECT_EQ(crossing().at(kArduCopter).samples.size(), 146U);
  EXPECT_EQ(crossing().at(kPx4).positions.size(), 1492U);
  EXPECT_EQ(crossing().at(kArduCopter).positions.size(), 1530U);
}

TEST(CrossingClocks, Px4SihClockDriftsSoIsNotChecked) {
  const Vehicle& px4 = crossing().at(kPx4);
  const Result<ClockFit> fit = fit_clock(px4.samples);
  ASSERT_TRUE(fit.has_value());
  // Its boot clock is simulated time, about 2.5 % slower than the host clock
  // that gives its UTC, and uneven.
  EXPECT_GT(fit->model.drift_ppm(), 20'000.0);
  EXPECT_GT(fit->residual_rms.count(), 5'000'000.0);
  EXPECT_FALSE(straight(*fit));
  const Result<DriftCheck> check =
      check_injected_drift(px4.samples, px4.positions, Injection{.ppm = 100.0, .offset = seconds(5)}, kOutage);
  ASSERT_TRUE(check.has_value());
  EXPECT_GT(check->reference_spread.count(), 1e9);
  EXPECT_EQ(judge(*check), Outcome::kDrifting);
}

TEST(CrossingClocks, ArduCopterAlignsWithinAMillisecondWithAnInjectedDrift) {
  const Vehicle& copter = crossing().at(kArduCopter);
  const Result<ClockFit> fit = fit_clock(copter.samples);
  ASSERT_TRUE(fit.has_value());
  EXPECT_TRUE(straight(*fit));
  EXPECT_NEAR(fit->model.drift_ppm(), 0.0, 1.0);
  for (const Injection& injection :
       {Injection{.ppm = 100.0, .offset = seconds(5)}, Injection{.ppm = -100.0, .offset = seconds(2)}}) {
    const Result<DriftCheck> check = check_injected_drift(copter.samples, copter.positions, injection, kOutage);
    ASSERT_TRUE(check.has_value()) << injection.ppm;
    EXPECT_LT(check->reference_spread, kMaxReferenceSpread) << injection.ppm;
    EXPECT_EQ(judge(*check), Outcome::kWithin) << injection.ppm;
    // Well within: about 20 us.
    EXPECT_LT(check->max_error, microseconds(100)) << injection.ppm;
    EXPECT_NEAR(check->fit.model.drift_ppm(), -injection.ppm, 1.0) << injection.ppm;
  }
}

}  // namespace
}  // namespace ics::timealign
