#include "ics/capd/utc_offset.hpp"

#include <chrono>
#include <string>

#include <gtest/gtest.h>

#include "capd_support.hpp"
#include "support.hpp"

namespace {

using ics::capd::PtpConfig;
using ics::capd::read_tai_minus_utc;
using ics::capd::testing::AnsweringPtp4l;
using ics::timing::testing::TempDir;

PtpConfig ptp_config(const TempDir& dir) { return PtpConfig{dir / "ptp4l-ro", dir / "client", 0}; }

TEST(UtcOffset, ReadsTaiMinusUtcFromPtp4l) {
  const TempDir dir;
  const AnsweringPtp4l ptp4l(dir / "ptp4l-ro", dir / "client", ics::capd::testing::kOffset37);
  EXPECT_EQ(read_tai_minus_utc(ptp_config(dir)).value(), std::chrono::seconds(37));
}

TEST(UtcOffset, RefusesAnOffsetPtp4lDoesNotMarkValid) {
  const TempDir dir;
  const AnsweringPtp4l ptp4l(dir / "ptp4l-ro", dir / "client", ics::capd::testing::kOffsetInvalid);
  EXPECT_EQ(read_tai_minus_utc(ptp_config(dir)).error(), ics::Error::kUnavailable);
}

TEST(UtcOffset, FailsWithoutPtp4l) {
  const TempDir dir;
  EXPECT_EQ(read_tai_minus_utc(ptp_config(dir)).error(), ics::Error::kUnavailable);
  const PtpConfig too_long{std::string(200, 'x'), dir / "client", 0};
  EXPECT_EQ(read_tai_minus_utc(too_long).error(), ics::Error::kInvalidArgument);
}

}  // namespace
