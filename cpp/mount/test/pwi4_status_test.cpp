#include "ics/mount/pwi4_status.hpp"

#include <cmath>
#include <numbers>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

namespace {

using ics::mount::Axis;
using ics::mount::parse_status;

constexpr double kRadPerDeg = std::numbers::pi / 180.0;

// A status as PWI4 4.1 writes it, with keywords the parser does not use.
const std::string kStatus =
    "pwi4.version=4.1.9 beta 22\r\n"
    "response.timestamp_utc=2026-10-11 17:59:43.925011\n"
    "mount.is_connected=true\n"
    "mount.is_slewing=false\n"
    "mount.is_tracking=true\n"
    "mount.distance_to_sun_degs=95.5\n"
    "mount.axis0.is_enabled=true\n"
    "mount.axis0.position_degs=123.25\n"
    "mount.axis0.measured_velocity_degs_per_sec=-1.5\n"
    "mount.axis0.min_mech_position_degs=-270\n"
    "mount.axis0.max_mech_position_degs=270\n"
    "mount.axis0.rms_error_arcsec=0.0008\n"
    "mount.axis1.is_enabled=false\n"
    "mount.axis1.position_degs=45\n"
    "mount.axis1.measured_velocity_degs_per_sec=0.25\n"
    "mount.axis1.min_mech_position_degs=0\n"
    "mount.axis1.max_mech_position_degs=90\n"
    "\n";

// The status with one keyword's line replaced.
std::string with(const std::string_view key, const std::string_view line) {
  std::string out = kStatus;
  const std::size_t at = out.find(std::string(key) + "=");
  out.replace(at, out.find('\n', at) - at, line);
  return out;
}

void expect_malformed(const std::string& text) {
  const auto status = parse_status(text);
  ASSERT_FALSE(status.has_value()) << text;
  EXPECT_EQ(status.error(), ics::Error::kMalformed);
}

TEST(MountStatus, ReadsWhatTheClientUses) {
  const auto status = parse_status(kStatus);
  ASSERT_TRUE(status.has_value());
  EXPECT_TRUE(status->connected);
  EXPECT_FALSE(status->slewing);
  EXPECT_TRUE(status->tracking);
  EXPECT_DOUBLE_EQ(status->distance_to_sun.value(), 95.5 * kRadPerDeg);
  const auto& azimuth = status->axes[static_cast<std::size_t>(Axis::kAzimuth)];
  EXPECT_TRUE(azimuth.enabled);
  EXPECT_DOUBLE_EQ(azimuth.position.value(), 123.25 * kRadPerDeg);
  EXPECT_DOUBLE_EQ(azimuth.velocity_rad_per_s, -1.5 * kRadPerDeg);
  EXPECT_DOUBLE_EQ(azimuth.min_position.value(), -270.0 * kRadPerDeg);
  EXPECT_DOUBLE_EQ(azimuth.max_position.value(), 270.0 * kRadPerDeg);
  const auto& altitude = status->axes[static_cast<std::size_t>(Axis::kAltitude)];
  EXPECT_FALSE(altitude.enabled);
  EXPECT_DOUBLE_EQ(altitude.position.value(), 45.0 * kRadPerDeg);
  EXPECT_DOUBLE_EQ(altitude.max_position.value(), 90.0 * kRadPerDeg);
}

TEST(MountStatus, TakesTheFirstOfARepeatedKeyword) {
  // The last line without its newline.
  const auto status = parse_status("mount.is_connected=false\n" + kStatus + "pwi4.unused=1");
  ASSERT_TRUE(status.has_value());
  EXPECT_FALSE(status->connected);
}

TEST(MountStatus, RefusesMalformedText) {
  expect_malformed(kStatus + "no equals sign\n");
  expect_malformed(with("mount.is_slewing", "mount.is_slewing=yes"));
  expect_malformed(with("mount.axis0.position_degs", "mount.axis0.position_degs=12.5x"));
  expect_malformed(with("mount.axis0.position_degs", "mount.axis0.position_degs=abc"));
  expect_malformed(with("mount.axis1.position_degs", "mount.axis1.position_degs=inf"));
  expect_malformed(with("mount.axis1.max_mech_position_degs", "mount.unrelated=1"));
  expect_malformed(std::string(ics::mount::kMaxStatusBytes + 1, '\n'));
  expect_malformed("");
}

}  // namespace
