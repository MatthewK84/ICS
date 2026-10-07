#include "ics/camera/emulated_x6980.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "ics/camera/flir_sdk.hpp"
#include "ics/camera/irig.hpp"
#include "ics/camera/segment_camera.hpp"
#include "ics/camera/strobe.hpp"
#include "ics/camera/trigger_schedule.hpp"
#include "ics/common/error.hpp"
#include "ics/common/units.hpp"

namespace ics::camera {
namespace {

using std::chrono::microseconds;
using std::chrono::milliseconds;

constexpr std::int64_t kEpochNs = 1'791'331'200'000'000'000;

// A 32 x 16 window at (600, 490), 1,004 frames/s, 100 frames a segment, 60 of
// them after the trigger.
CameraSettings settings() {
  return CameraSettings{.width = 32,
                        .height = 16,
                        .window_x = 600,
                        .window_y = 490,
                        .frame_rate = 1004,
                        .exposure = microseconds(500),
                        .segment_frames = 100,
                        .post_trigger_frames = 60,
                        .irig = true};
}

// Triggers 150 ms apart, the first 0.3 µs past a microsecond.
const TriggerSchedule kSchedule{.first = utc_from_ns(kEpochNs + 300), .interval = milliseconds(150)};

TEST(EmulatedX6980, AcceptsOnlyWindowsOnItsSensorAndTimingItCanRecord) {
  const std::vector<std::pair<std::string, std::function<void(CameraSettings&)>>> refused{
      {"no width", [](CameraSettings& s) { s.width = 0; }},
      {"no height", [](CameraSettings& s) { s.height = 0; }},
      {"past the right edge", [](CameraSettings& s) { s.window_x = 609; }},
      {"past the bottom edge", [](CameraSettings& s) { s.window_y = 497; }},
      {"offset past any width", [](CameraSettings& s) { s.window_x = 0xFFFF'FFF0U; }},
      {"no frame rate", [](CameraSettings& s) { s.frame_rate = 0; }},
      {"integration of a period", [](CameraSettings& s) { s.exposure = microseconds(997); }},
      {"overlapping segments", [](CameraSettings& s) { s.segment_frames = 151; s.post_trigger_frames = 0; }},
  };
  for (const auto& [name, change] : refused) {
    EmulatedX6980 camera(kSchedule, true);
    CameraSettings s = settings();
    change(s);
    EXPECT_EQ(camera.configure(s).error(), Error::kInvalidArgument) << name;
  }
  EmulatedX6980 camera(kSchedule, true);
  CameraSettings full = settings();
  full.width = kX6980SensorWidth;
  full.height = kX6980SensorHeight;
  full.window_x = 0;
  full.window_y = 0;
  EXPECT_TRUE(camera.configure(full).has_value());
  const Result<CameraSettings> applied = camera.configure(settings());
  ASSERT_TRUE(applied.has_value());
  EXPECT_EQ(applied->window_x, 600U);
  EXPECT_EQ(applied->window_y, 490U);
}

TEST(EmulatedX6980, StampsEachTriggerOnSchedule) {
  EmulatedX6980 camera(kSchedule, false);
  EXPECT_EQ(camera.arm(1).error(), Error::kInvalidArgument);
  EXPECT_EQ(camera.trigger().error(), Error::kInvalidArgument);
  ASSERT_TRUE(camera.configure(settings()).has_value());
  EXPECT_EQ(camera.arm(65).error(), Error::kInvalidArgument);
  ASSERT_TRUE(camera.arm(2).has_value());
  ASSERT_TRUE(camera.trigger().has_value());
  const Result<FlirTrigger> second = camera.trigger();
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(second->segment, 1U);
  EXPECT_EQ(second->recorded.first, -40);
  EXPECT_EQ(second->recorded.count, 100U);
  EXPECT_EQ(second->stamp.year, std::nullopt);
  // The stamp keeps the microsecond and drops the 0.3 µs past it.
  const UtcTime expected = utc_from_ns(kEpochNs) + milliseconds(150);
  EXPECT_EQ(utc_from_irig(second->stamp, expected).value(), expected);
  EXPECT_EQ(camera.trigger().error(), Error::kFull);
}

TEST(EmulatedX6980, ReadsTheFramesASegmentHolds) {
  EmulatedX6980 camera(kSchedule, true);
  ASSERT_TRUE(camera.configure(settings()).has_value());
  ASSERT_TRUE(camera.arm(1).has_value());
  EXPECT_EQ(camera.read(0, FrameRange{.first = -40, .count = 1}).error(), Error::kOutOfRange);
  ASSERT_TRUE(camera.trigger().has_value());
  EXPECT_EQ(camera.read(1, FrameRange{.first = 0, .count = 1}).error(), Error::kOutOfRange);
  EXPECT_EQ(camera.read(0, FrameRange{.first = -41, .count = 1}).error(), Error::kOutOfRange);
  EXPECT_EQ(camera.read(0, FrameRange{.first = 59, .count = 2}).error(), Error::kOutOfRange);
  EXPECT_EQ(camera.read(0, FrameRange{.first = 0, .count = 0}).error(), Error::kOutOfRange);
  const Result<std::vector<FlirFrame>> frames = camera.read(0, FrameRange{.first = -1, .count = 3});
  ASSERT_TRUE(frames.has_value());
  ASSERT_EQ(frames->size(), 3U);
  const FlirFrame& last = frames->back();
  EXPECT_EQ(last.number, 1);
  EXPECT_EQ(last.integration, microseconds(500));
  EXPECT_EQ(last.stamp.year, std::optional<std::uint16_t>(2026));
  // 1 / 1,004 s after the trigger is 996,015.9 ns: 996,016 ns, at 996,316 ns
  // past the microsecond, stamped 996 µs after it.
  EXPECT_EQ(utc_from_irig(last.stamp, {}).value(), utc_from_ns(kEpochNs) + microseconds(996));
  // Without a scene, every pixel is 0.
  ASSERT_EQ(last.pixels.size(), 32U * 16U * 2U);
  EXPECT_EQ(last.pixels[0], std::byte{0});
  EXPECT_EQ(last.pixels[1], std::byte{0});
}

// A 50 µs strobe at each second's start, lighting the first 4 x 2 pixels at
// 10 counts a microsecond over a background of 100, seen by a camera that
// stamps 3 µs late.
StrobeScene scene() {
  return StrobeScene{
      .schedule = StrobeSchedule{.pulse_width = microseconds(50), .latency = {}, .delay_step = {}, .sweep_steps = 1},
      .roi = Roi{.x = 0, .y = 0, .width = 4, .height = 2},
      .stamp_offset = microseconds(3),
      .background = 100.0,
      .gain_per_us = 10.0,
      .noise_sigma = 0.0,
      .seed = 0};
}

std::uint32_t pixel(const FlirFrame& frame, const std::size_t index) {
  return std::to_integer<std::uint32_t>(frame.pixels[2 * index]) |
         (std::to_integer<std::uint32_t>(frame.pixels[(2 * index) + 1]) << 8U);
}

TEST(EmulatedX6980, LightsItsPixelsAsTheSceneAndStampsLate) {
  EmulatedX6980 camera(kSchedule, true, scene());
  ASSERT_TRUE(camera.configure(settings()).has_value());
  ASSERT_TRUE(camera.arm(1).has_value());
  const Result<FlirTrigger> fired = camera.trigger();
  ASSERT_TRUE(fired.has_value());
  EXPECT_EQ(utc_from_irig(fired->stamp, {}).value(), utc_from_ns(kEpochNs) + microseconds(3));
  const Result<std::vector<FlirFrame>> frames = camera.read(0, FrameRange{.first = -1, .count = 2});
  ASSERT_TRUE(frames.has_value());
  const FlirFrame& lit = frames->back();
  EXPECT_EQ(utc_from_irig(lit.stamp, {}).value(), utc_from_ns(kEpochNs) + microseconds(3));
  // Frame 0's exposure starts 0.3 µs into the pulse: 49.7 µs of it, 597
  // counts in the rectangle; 100 beside it, and in frame -1, before it.
  EXPECT_EQ(pixel(lit, 0), 597U);
  EXPECT_EQ(pixel(lit, 32 + 3), 597U);
  EXPECT_EQ(pixel(lit, 4), 100U);
  EXPECT_EQ(pixel(lit, 2 * 32), 100U);
  EXPECT_EQ(pixel(frames->front(), 0), 100U);
}

TEST(EmulatedX6980, RefusesASceneThatDoesNotSuitTheSettings) {
  StrobeScene outside = scene();
  outside.roi.x = 29;
  StrobeScene no_pulse = scene();
  no_pulse.schedule.pulse_width = {};
  StrobeScene negative = scene();
  negative.gain_per_us = -1.0;
  for (const StrobeScene& bad : {outside, no_pulse, negative}) {
    EmulatedX6980 camera(kSchedule, true, bad);
    EXPECT_EQ(camera.configure(settings()).error(), Error::kInvalidArgument);
  }
}

}  // namespace
}  // namespace ics::camera
