#include "ics/mount/pwi4_client.hpp"

#include <chrono>
#include <cstddef>
#include <string>
#include <utility>

#include <gtest/gtest.h>

#include "emulated_pwi4.hpp"
#include "mount_support.hpp"

namespace {

using ics::Radians;
using ics::mount::Axis;
using ics::mount::Pwi4Client;
using ics::mount::Pwi4Settings;
using ics::mount::testing::client_for;
using ics::mount::testing::EmulatedPwi4;
using ics::mount::testing::kDeg;
using ics::mount::testing::soon;
using std::chrono::milliseconds;

TEST(Pwi4Client, RefusesBadSettings) {
  EmulatedPwi4 pwi4;
  const Pwi4Settings good = ics::mount::testing::settings_for(pwi4);
  Pwi4Settings no_port = good;
  no_port.port = 0;
  Pwi4Settings no_attempts = good;
  no_attempts.retry.attempts = 0;
  Pwi4Settings no_timeout = good;
  no_timeout.timeout = milliseconds::zero();
  for (const Pwi4Settings& bad : {no_port, no_attempts, no_timeout}) {
    EXPECT_EQ(Pwi4Client::make(bad).error(), ics::Error::kInvalidArgument);
  }
}

TEST(Pwi4Client, ConnectsEnablesAndReadsTheStatus) {
  EmulatedPwi4 pwi4;
  Pwi4Client client = client_for(pwi4);
  EXPECT_FALSE(client.status(soon())->connected);
  ASSERT_TRUE(client.connect(soon()).has_value());
  ASSERT_TRUE(client.enable(Axis::kAzimuth, soon()).has_value());
  ASSERT_TRUE(client.enable(Axis::kAltitude, soon()).has_value());
  const auto status = client.status(soon());
  ASSERT_TRUE(status.has_value());
  EXPECT_TRUE(status->connected);
  EXPECT_TRUE(status->axes[0].enabled);
  EXPECT_TRUE(status->axes[1].enabled);
  EXPECT_NEAR(status->axes[1].position.value(), 30 * kDeg, 1e-6);
  EXPECT_EQ(pwi4.seen().enables, 2U);
  // One connection, kept open.
  EXPECT_EQ(client.stats().attempts, client.stats().requests);
}

TEST(Pwi4Client, GoesToATargetWithinTheLimits) {
  EmulatedPwi4 pwi4;
  Pwi4Client client = client_for(pwi4);
  ASSERT_TRUE(client.goto_alt_az({.altitude = Radians(45 * kDeg), .azimuth = Radians(-90 * kDeg)}, soon()));
  EXPECT_EQ(pwi4.seen().gotos, 1U);
  EXPECT_DOUBLE_EQ(pwi4.seen().goto_alt_deg, 45.0);
  EXPECT_DOUBLE_EQ(pwi4.seen().goto_az_deg, 270.0);
  ASSERT_TRUE(client.goto_alt_az({.altitude = Radians(20 * kDeg), .azimuth = Radians(400 * kDeg)}, soon()));
  EXPECT_DOUBLE_EQ(pwi4.seen().goto_az_deg, 40.0);
  // Below the 5° altitude limit: refused, and never sent.
  EXPECT_EQ(client.goto_alt_az({.altitude = Radians(2 * kDeg), .azimuth = Radians(0)}, soon()).error(),
            ics::Error::kOutOfRange);
  EXPECT_EQ(pwi4.seen().gotos, 2U);
}

TEST(Pwi4Client, SendsNoGotoWithoutTheStatus) {
  EmulatedPwi4 pwi4;
  Pwi4Client client = client_for(pwi4);
  pwi4.fail_next(3);
  EXPECT_EQ(client.goto_alt_az({.altitude = Radians(45 * kDeg), .azimuth = Radians(0)}, soon()).error(),
            ics::Error::kUnavailable);
  EXPECT_EQ(pwi4.seen().gotos, 0U);
}

TEST(Pwi4Client, SendsRatesHeldToTheLimits) {
  EmulatedPwi4 pwi4;
  Pwi4Client client = client_for(pwi4);
  const auto status = client.status(soon());
  ASSERT_TRUE(status.has_value());
  const auto applied =
      client.set_rates({.rad_per_s = {25 * kDeg, -1.5 * kDeg}}, *status, soon());
  ASSERT_TRUE(applied.has_value());
  EXPECT_TRUE(applied->clamped[0]);
  EXPECT_FALSE(applied->clamped[1]);
  EXPECT_NEAR(pwi4.seen().last_rate_deg_s[0], 10.0, 1e-9);
  EXPECT_NEAR(pwi4.seen().last_rate_deg_s[1], -1.5, 1e-9);
  ASSERT_TRUE(client.stop_rates(soon()).has_value());
  EXPECT_EQ(pwi4.seen().last_rate_deg_s[0], 0.0);
  ASSERT_TRUE(client.stop(soon()).has_value());
  EXPECT_EQ(pwi4.seen().stops, 1U);
  pwi4.fail_next(3);
  EXPECT_EQ(client.set_rates({.rad_per_s = {0.0, 0.0}}, *status, soon()).error(), ics::Error::kUnavailable);
}

TEST(Pwi4Client, RetriesAFailedAttempt) {
  EmulatedPwi4 pwi4;
  Pwi4Client client = client_for(pwi4);
  pwi4.fail_next(1);
  EXPECT_TRUE(client.status(soon()).has_value());
  EXPECT_EQ(client.stats().attempts, 2U);
  // libcurl tries a dead kept-open connection once more by itself, so two
  // dropped answers cost one or two attempts.
  pwi4.drop_next(2);
  EXPECT_TRUE(client.status(soon()).has_value());
  EXPECT_GE(client.stats().attempts, 4U);
  EXPECT_EQ(client.stats().failures, 0U);
}

TEST(Pwi4Client, GivesUpAfterEveryAttempt) {
  EmulatedPwi4 pwi4;
  Pwi4Client client = client_for(pwi4);
  pwi4.fail_next(3);
  EXPECT_EQ(client.connect(soon()).error(), ics::Error::kUnavailable);
  EXPECT_EQ(client.stats().attempts, 3U);
  EXPECT_EQ(client.stats().failures, 1U);
  EXPECT_EQ(client.stats().last_error, "HTTP 500");
}

TEST(Pwi4Client, NeverWaitsPastTheDeadline) {
  EmulatedPwi4 pwi4;
  Pwi4Client client = client_for(pwi4);
  pwi4.fail_next(3);
  // The 5 ms wait before a second attempt would pass the deadline.
  EXPECT_EQ(client.stop(std::chrono::steady_clock::now() + milliseconds(3)).error(), ics::Error::kUnavailable);
  EXPECT_EQ(client.stats().attempts, 1U);
  // A deadline already passed sends nothing.
  EXPECT_EQ(client.stop(std::chrono::steady_clock::now() - milliseconds(1)).error(), ics::Error::kUnavailable);
  EXPECT_EQ(client.stats().attempts, 1U);
  EXPECT_EQ(client.stats().last_error, "deadline passed");
}

TEST(Pwi4Client, TimesOutAnAttempt) {
  EmulatedPwi4 pwi4;
  Pwi4Client client = client_for(pwi4);
  pwi4.delay(milliseconds(50));
  EXPECT_EQ(client.status(soon()).error(), ics::Error::kUnavailable);
  EXPECT_NE(client.stats().last_error.find("Timeout"), std::string::npos) << client.stats().last_error;
}

TEST(Pwi4Client, RefusesAStatusItCannotRead) {
  EmulatedPwi4 pwi4;
  Pwi4Client client = client_for(pwi4);
  pwi4.status_text("mount.is_connected=maybe\n");
  EXPECT_EQ(client.status(soon()).error(), ics::Error::kMalformed);
  pwi4.status_text(std::string(ics::mount::kMaxStatusBytes + 1, 'x'));
  EXPECT_EQ(client.status(soon()).error(), ics::Error::kUnavailable);
}

TEST(Pwi4Client, Moves) {
  EmulatedPwi4 pwi4;
  Pwi4Client first = client_for(pwi4);
  Pwi4Client second = client_for(pwi4);
  second = std::move(first);
  EXPECT_TRUE(second.status(soon()).has_value());
}

}  // namespace
