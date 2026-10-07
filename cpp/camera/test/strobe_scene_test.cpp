#include "ics/camera/strobe.hpp"

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

#include <gtest/gtest.h>

#include "ics/camera/cine.hpp"
#include "ics/camera/emulated_phantom.hpp"
#include "ics/camera/image.hpp"
#include "ics/camera/mapped_file.hpp"
#include "ics/camera/segment_camera.hpp"
#include "ics/camera/trigger_schedule.hpp"
#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "scratch_dir.hpp"

namespace ics::camera {
namespace {

using std::chrono::microseconds;
using std::chrono::milliseconds;
using std::chrono::seconds;
using testing_support::ScratchDir;

constexpr std::int64_t kEpochSeconds = 1'791'331'200;

// 20 µs pulses, 1.5 µs after each PPS edge, swept by 5 µs a second over 40
// seconds.
StrobeSchedule swept() {
  return StrobeSchedule{
      .pulse_width = microseconds(20), .latency = Duration(1500), .delay_step = microseconds(5), .sweep_steps = 40};
}

TEST(StrobeSchedule, SweepsEachSecondAndRepeats) {
  const StrobeSchedule s = swept();
  // 1,791,331,200 is a multiple of 40: the sweep's first step.
  EXPECT_EQ(s.pulse_start(kEpochSeconds), UtcTime(seconds(kEpochSeconds)) + Duration(1500));
  EXPECT_EQ(s.pulse_start(kEpochSeconds + 3), UtcTime(seconds(kEpochSeconds + 3)) + Duration(16'500));
  EXPECT_EQ(s.pulse_start(kEpochSeconds + 40), UtcTime(seconds(kEpochSeconds + 40)) + Duration(1500));
  // Before 1970 the steps run on: -1 is step 39.
  EXPECT_EQ(s.pulse_start(-1), UtcTime(seconds(-1)) + Duration(1500) + microseconds(195));
  StrobeSchedule none = s;
  none.sweep_steps = 0;
  EXPECT_EQ(none.pulse_start(kEpochSeconds + 3), UtcTime(seconds(kEpochSeconds + 3)) + Duration(1500));
}

TEST(StrobeSchedule, IsValidOnlyWithAPulseThatEndsWithinItsSecond) {
  EXPECT_TRUE(swept().valid());
  std::vector<StrobeSchedule> invalid(6, swept());
  invalid[0].pulse_width = {};
  invalid[1].latency = Duration(-1);
  invalid[2].delay_step = Duration(-1);
  invalid[3].sweep_steps = 0;
  invalid[4].sweep_steps = 3601;
  invalid[5].latency = seconds(1) - microseconds(215);
  for (std::size_t i = 0; i < invalid.size(); ++i) {
    EXPECT_FALSE(invalid[i].valid()) << i;
  }
}

TEST(StrobeLight, IsThePulseTimeWithinTheExposure) {
  const StrobeSchedule s = swept();
  const UtcTime second{seconds(kEpochSeconds)};
  // The pulse lights 1.5 µs to 21.5 µs past the second.
  EXPECT_EQ(strobe_light(s, second, microseconds(150)), microseconds(20));
  EXPECT_EQ(strobe_light(s, second + microseconds(10), microseconds(150)), Duration(11'500));
  EXPECT_EQ(strobe_light(s, second - microseconds(149), microseconds(150)), Duration(0));
  EXPECT_EQ(strobe_light(s, second + microseconds(30), microseconds(150)), Duration(0));
  // An exposure late in a second sees the next second's pulse.
  EXPECT_EQ(strobe_light(s, second - microseconds(140), microseconds(150)), Duration(8'500));
}

// A Phantom that stamps 37.4 µs late, seeing a 40 µs pulse at each second
// over 4 x 4 pixels, with a noise of 30 counts.
StrobeScene scene() {
  return StrobeScene{
      .schedule = StrobeSchedule{.pulse_width = microseconds(40), .latency = {}, .delay_step = {}, .sweep_steps = 1},
      .roi = Roi{.x = 2, .y = 1, .width = 4, .height = 4},
      .stamp_offset = Duration(37'400),
      .background = 200.0,
      .gain_per_us = 50.0,
      .noise_sigma = 30.0,
      .seed = 7};
}

CameraSettings settings() {
  return CameraSettings{.width = 8,
                        .height = 8,
                        .window_x = 0,
                        .window_y = 0,
                        .frame_rate = 5000,
                        .exposure = microseconds(150),
                        .segment_frames = 20,
                        .post_trigger_frames = 10,
                        .irig = true};
}

TEST(EmulatedPhantom, LightsItsImagesAsTheSceneAndStampsLate) {
  const ScratchDir dir;
  const UtcTime second{seconds(kEpochSeconds)};
  EmulatedPhantom camera(TriggerSchedule{.first = second, .interval = seconds(1)}, scene());
  ASSERT_TRUE(camera.configure(settings()).has_value());
  ASSERT_TRUE(camera.arm(1).has_value());
  EXPECT_EQ(camera.trigger().value().trigger_time, second + Duration(37'400));
  const std::filesystem::path path = dir.path() / "strobe.cine";
  ASSERT_TRUE(camera.save(0, FrameRange{.first = -1, .count = 2}, path).has_value());
  const Result<MappedFile> file = MappedFile::open(path);
  const Cine cine = read_cine(file.value().bytes()).value();
  EXPECT_EQ(cine.trigger_time, second + Duration(37'400));
  EXPECT_EQ(cine.frame_times[1], second + Duration(37'400));
  // Frame 0 holds the whole pulse: 200 + 50 x 40, near 2,200 for 16 pixels'
  // noise; frame -1 holds none of it.
  EXPECT_NEAR(roi_mean(file.value().bytes(), cine, 1, scene().roi).value(), 2200.0, 30.0);
  EXPECT_NEAR(roi_mean(file.value().bytes(), cine, 0, scene().roi).value(), 200.0, 30.0);
  EXPECT_EQ(roi_mean(file.value().bytes(), cine, 1, Roi{.x = 0, .y = 0, .width = 2, .height = 1}).value(), 200.0);
  std::vector<StrobeScene> refused(5, scene());
  refused[0].roi.height = 8;
  refused[1].roi.x = 5;
  refused[2].background = -1.0;
  refused[3].noise_sigma = -1.0;
  refused[4].schedule.sweep_steps = 0;
  for (std::size_t i = 0; i < refused.size(); ++i) {
    EmulatedPhantom bad(TriggerSchedule{.first = second, .interval = seconds(1)}, refused[i]);
    EXPECT_EQ(bad.configure(settings()).error(), Error::kInvalidArgument) << i;
  }
}

TEST(EmulatedPhantom, TakesARectangleOfNoHeightAsNoStrobe) {
  const ScratchDir dir;
  StrobeScene flat = scene();
  flat.roi.height = 0;
  flat.schedule.pulse_width = {};
  EmulatedPhantom camera(TriggerSchedule{.first = UtcTime(seconds(kEpochSeconds)), .interval = seconds(1)}, flat);
  ASSERT_TRUE(camera.configure(settings()).has_value());
  ASSERT_TRUE(camera.arm(1).has_value());
  ASSERT_TRUE(camera.trigger().has_value());
  const std::filesystem::path path = dir.path() / "flat.cine";
  ASSERT_TRUE(camera.save(0, FrameRange{.first = 0, .count = 1}, path).has_value());
  const Result<MappedFile> file = MappedFile::open(path);
  const Cine cine = read_cine(file.value().bytes()).value();
  EXPECT_EQ(roi_mean(file.value().bytes(), cine, 0, Roi{.x = 0, .y = 0, .width = 8, .height = 8}).value(), 200.0);
}

TEST(EmulatedPhantom, DrawsNoiseOfTheScenesSigma) {
  const ScratchDir dir;
  StrobeScene wide = scene();
  wide.roi = Roi{.x = 0, .y = 0, .width = 64, .height = 64};
  CameraSettings s = settings();
  s.width = 64;
  s.height = 64;
  EmulatedPhantom camera(TriggerSchedule{.first = UtcTime(seconds(kEpochSeconds)), .interval = seconds(1)}, wide);
  ASSERT_TRUE(camera.configure(s).has_value());
  ASSERT_TRUE(camera.arm(1).has_value());
  ASSERT_TRUE(camera.trigger().has_value());
  const std::filesystem::path path = dir.path() / "noise.cine";
  ASSERT_TRUE(camera.save(0, FrameRange{.first = -5, .count = 1}, path).has_value());
  const Result<MappedFile> file = MappedFile::open(path);
  const Cine cine = read_cine(file.value().bytes()).value();
  const std::span<const std::byte> image = file.value().bytes().subspan(cine.image_offsets[0] + 8, 64 * 64 * 2);
  double sum = 0.0;
  double squares = 0.0;
  for (std::size_t i = 0; i < image.size(); i += 2) {
    const double v = std::to_integer<int>(image[i]) + (std::to_integer<int>(image[i + 1]) * 256);
    sum += v;
    squares += v * v;
  }
  const double mean = sum / 4096.0;
  EXPECT_NEAR(mean, 200.0, 2.0);
  EXPECT_NEAR(std::sqrt((squares / 4096.0) - (mean * mean)), 30.0, 2.0);
}

}  // namespace
}  // namespace ics::camera
