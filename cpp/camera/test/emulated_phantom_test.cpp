#include "ics/camera/emulated_phantom.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "ics/camera/cine.hpp"
#include "ics/camera/mapped_file.hpp"
#include "ics/camera/segment_camera.hpp"
#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "scratch_dir.hpp"

namespace ics::camera {
namespace {

using std::chrono::microseconds;
using std::chrono::milliseconds;
using testing_support::ScratchDir;

constexpr std::int64_t kEpochNs = 1'791'331'200'000'000'000;

// 4,000 frames/s, 100 frames a segment, 60 of them after the trigger.
CameraSettings settings() {
  return CameraSettings{.width = 32,
                        .height = 16,
                        .window_x = 0,
                        .window_y = 0,
                        .frame_rate = 4000,
                        .exposure = microseconds(200),
                        .segment_frames = 100,
                        .post_trigger_frames = 60,
                        .irig = true};
}

const TriggerSchedule kSchedule{.first = utc_from_ns(kEpochNs), .interval = milliseconds(30)};

TEST(EmulatedPhantom, AcceptsOnlySettingsItCanRecord) {
  const std::vector<std::pair<std::string, std::function<void(CameraSettings&)>>> refused{
      {"no width", [](CameraSettings& s) { s.width = 0; }},
      {"too wide", [](CameraSettings& s) { s.width = 65'536; }},
      {"no height", [](CameraSettings& s) { s.height = 0; }},
      {"too high", [](CameraSettings& s) { s.height = 65'536; }},
      {"no frame rate", [](CameraSettings& s) { s.frame_rate = 0; }},
      {"no exposure", [](CameraSettings& s) { s.exposure = Duration(0); }},
      {"exposure of a period", [](CameraSettings& s) { s.exposure = microseconds(250); }},
      {"no frames", [](CameraSettings& s) { s.segment_frames = 0; }},
      {"too many frames", [](CameraSettings& s) { s.segment_frames = (1U << 20U) + 1; }},
      {"more after than held", [](CameraSettings& s) { s.post_trigger_frames = 101; }},
      {"overlapping segments", [](CameraSettings& s) { s.segment_frames = 121; s.post_trigger_frames = 0; }},
  };
  for (const auto& [name, change] : refused) {
    EmulatedPhantom camera(kSchedule);
    CameraSettings s = settings();
    change(s);
    EXPECT_EQ(camera.configure(s).error(), Error::kInvalidArgument) << name;
  }
  EmulatedPhantom camera(kSchedule);
  CameraSettings offset = settings();
  offset.window_x = 8;
  offset.window_y = 4;
  const Result<CameraSettings> applied = camera.configure(offset);
  ASSERT_TRUE(applied.has_value());
  EXPECT_TRUE(applied->irig);
  EXPECT_EQ(applied->frame_rate, 4000U);
  // It reads the full sensor's position, whatever offset is asked.
  EXPECT_EQ(applied->window_x, 0U);
  EXPECT_EQ(applied->window_y, 0U);
}

TEST(EmulatedPhantom, ArmsThenTriggersEachSegmentOnSchedule) {
  EmulatedPhantom camera(kSchedule);
  EXPECT_EQ(camera.arm(2).error(), Error::kInvalidArgument);
  EXPECT_EQ(camera.trigger().error(), Error::kInvalidArgument);
  ASSERT_TRUE(camera.configure(settings()).has_value());
  EXPECT_EQ(camera.trigger().error(), Error::kInvalidArgument);
  EXPECT_EQ(camera.arm(0).error(), Error::kInvalidArgument);
  EXPECT_EQ(camera.arm(65).error(), Error::kInvalidArgument);
  ASSERT_TRUE(camera.arm(2).has_value());
  const Result<SegmentStatus> first = camera.trigger();
  const Result<SegmentStatus> second = camera.trigger();
  ASSERT_TRUE(first.has_value() && second.has_value());
  EXPECT_EQ(first->segment, 0U);
  EXPECT_EQ(second->segment, 1U);
  EXPECT_EQ(second->trigger_time, utc_from_ns(kEpochNs) + milliseconds(30));
  EXPECT_EQ(second->recorded.first, -40);
  EXPECT_EQ(second->recorded.count, 100U);
  EXPECT_EQ(camera.trigger().error(), Error::kFull);
}

TEST(EmulatedPhantom, SavesTheFramesOfASegmentAsACine) {
  const ScratchDir dir;
  EmulatedPhantom camera(kSchedule);
  ASSERT_TRUE(camera.configure(settings()).has_value());
  ASSERT_TRUE(camera.arm(2).has_value());
  ASSERT_TRUE(camera.trigger().has_value());
  ASSERT_TRUE(camera.trigger().has_value());
  const std::filesystem::path path = dir.path() / "cine-001.cine";
  ASSERT_TRUE(camera.save(1, FrameRange{.first = -10, .count = 30}, path).has_value());
  const Result<MappedFile> file = MappedFile::open(path);
  ASSERT_TRUE(file.has_value());
  const Result<Cine> cine = read_cine(file->bytes());
  ASSERT_TRUE(cine.has_value());
  EXPECT_EQ(cine->first_image_no, -10);
  EXPECT_EQ(cine->trigger_time, utc_from_ns(kEpochNs) + milliseconds(30));
  ASSERT_EQ(cine->frame_times.size(), 30U);
  EXPECT_EQ(cine->frame_times[10], cine->trigger_time);
  EXPECT_EQ(cine->frame_times[11] - cine->frame_times[10], microseconds(250));
  EXPECT_EQ(cine->exposures[0], microseconds(200));
}

TEST(EmulatedPhantom, RefusesToSaveFramesItDoesNotHoldOrCannotWrite) {
  const ScratchDir dir;
  EmulatedPhantom camera(kSchedule);
  ASSERT_TRUE(camera.configure(settings()).has_value());
  ASSERT_TRUE(camera.arm(1).has_value());
  const std::filesystem::path path = dir.path() / "c.cine";
  EXPECT_EQ(camera.save(0, FrameRange{.first = 0, .count = 1}, path).error(), Error::kOutOfRange);
  ASSERT_TRUE(camera.trigger().has_value());
  EXPECT_EQ(camera.save(1, FrameRange{.first = 0, .count = 1}, path).error(), Error::kOutOfRange);
  EXPECT_EQ(camera.save(0, FrameRange{.first = 0, .count = 0}, path).error(), Error::kOutOfRange);
  EXPECT_EQ(camera.save(0, FrameRange{.first = -41, .count = 1}, path).error(), Error::kOutOfRange);
  EXPECT_EQ(camera.save(0, FrameRange{.first = 0, .count = 61}, path).error(), Error::kOutOfRange);
  EXPECT_EQ(camera.save(0, FrameRange{.first = 0, .count = 1}, dir.path() / "missing" / "c.cine").error(),
            Error::kUnwritable);
  // A camera that cannot be saved as a cine: too many pixels for one image.
  EmulatedPhantom huge(kSchedule);
  CameraSettings big = settings();
  big.width = big.height = 65'535;
  ASSERT_TRUE(huge.configure(big).has_value());
  ASSERT_TRUE(huge.arm(1).has_value());
  ASSERT_TRUE(huge.trigger().has_value());
  EXPECT_EQ(huge.save(0, FrameRange{.first = 0, .count = 1}, path).error(), Error::kInvalidArgument);
  EXPECT_FALSE(std::filesystem::exists(path));
}

}  // namespace
}  // namespace ics::camera
