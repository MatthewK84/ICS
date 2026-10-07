#include "ics/strobe/fit.hpp"

#include <chrono>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "ics/camera/strobe.hpp"
#include "ics/common/error.hpp"
#include "ics/common/units.hpp"

namespace ics::strobe {
namespace {

using std::chrono::microseconds;
using std::chrono::milliseconds;
using std::chrono::seconds;

constexpr std::int64_t kEpochSeconds = 1'791'331'200;

// 20 µs pulses 1.5 µs after each PPS edge, swept by 5 µs a second over 40
// seconds.
camera::StrobeSchedule swept() {
  return camera::StrobeSchedule{
      .pulse_width = microseconds(20), .latency = Duration(1500), .delay_step = microseconds(5), .sweep_steps = 40};
}

// A 5,000 frame/s camera's frames around each of 40 seconds' PPS, exposed for
// 150 µs and stamped offset late: 200 counts, 50 a microsecond of strobe
// light, and up to 3 counts of a fixed pattern of noise.
std::vector<StrobeFrame> frames(const camera::StrobeSchedule& schedule, const Duration offset,
                                const double noise_scale = 1.0) {
  std::vector<StrobeFrame> out;
  for (std::int64_t k = 0; k < 40; ++k) {
    for (std::int64_t n = -10; n < 10; ++n) {
      const UtcTime start = UtcTime(seconds(kEpochSeconds + k)) + (microseconds(200) * n);
      const auto light = std::chrono::duration<double, std::micro>(
          camera::strobe_light(schedule, start, microseconds(150)));
      const double noise = (static_cast<double>(((k * 31) + (n * 17) + 1000) % 13) * 0.5 - 3.0) * noise_scale;
      out.push_back(StrobeFrame{
          .stamp = start + offset, .exposure = microseconds(150), .brightness = 200.0 + (50.0 * light.count()) + noise});
    }
  }
  return out;
}

TEST(FitOffset, FindsTheOffsetTheFramesWereStampedWith) {
  for (const Duration offset : {Duration(37'400), Duration(-112'600), Duration(0)}) {
    const Result<OffsetFit> fit = fit_offset(frames(swept(), offset), swept(), microseconds(500));
    ASSERT_TRUE(fit.has_value()) << offset.count();
    EXPECT_NEAR(static_cast<double>(fit->offset.count()), static_cast<double>(offset.count()), 200.0);
    EXPECT_GE(fit->sigma, Duration(1));
    EXPECT_LT(fit->sigma, microseconds(1));
    EXPECT_NEAR(fit->gain_per_us, 50.0, 1.0);
    EXPECT_NEAR(fit->background, 200.0, 1.0);
    EXPECT_LT(fit->rms, 3.0);
    EXPECT_EQ(fit->frames, 800U);
    EXPECT_GT(fit->edge_frames, 0U);
  }
}

TEST(FitOffset, RefusesFramesThatDoNotPinTheOffset) {
  EXPECT_EQ(fit_offset(std::vector<StrobeFrame>(3), swept(), microseconds(500)).error(), Error::kEmpty);
  // No sweep: each 20 µs pulse falls whole within one 150 µs exposure.
  camera::StrobeSchedule fixed = swept();
  fixed.sweep_steps = 1;
  fixed.latency = microseconds(50);
  EXPECT_EQ(fit_offset(frames(fixed, Duration(0)), fixed, microseconds(500)).error(), Error::kUnconstrained);
  // An offset beyond the search puts the best fit at its edge.
  EXPECT_EQ(fit_offset(frames(swept(), milliseconds(1)), swept(), microseconds(500)).error(), Error::kUnconstrained);
  // A best fit at either end of the search: the true offset lies beyond.
  EXPECT_EQ(fit_offset(frames(swept(), Duration(37'400)), swept(), microseconds(30)).error(), Error::kUnconstrained);
  EXPECT_EQ(fit_offset(frames(swept(), Duration(-37'400)), swept(), microseconds(30)).error(), Error::kUnconstrained);
  // A strobe 60 times noisier: a full pulse under ten times the residuals.
  EXPECT_EQ(fit_offset(frames(swept(), Duration(0), 60.0), swept(), microseconds(500)).error(), Error::kUnconstrained);
  // Frames nowhere near a pulse: no light to fit.
  std::vector<StrobeFrame> unlit = frames(swept(), Duration(0));
  for (StrobeFrame& frame : unlit) {
    frame.stamp += milliseconds(300);
  }
  EXPECT_EQ(fit_offset(unlit, swept(), microseconds(500)).error(), Error::kUnconstrained);
  // No light at all: every frame the same.
  std::vector<StrobeFrame> dark = frames(swept(), Duration(0));
  for (StrobeFrame& frame : dark) {
    frame.brightness = 200.0;
  }
  EXPECT_EQ(fit_offset(dark, swept(), microseconds(500)).error(), Error::kUnconstrained);
}

}  // namespace
}  // namespace ics::strobe
