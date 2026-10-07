#include "ics/camera/offload.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

#include <gtest/gtest.h>

#include "ics/camera/cine.hpp"
#include "ics/camera/emulated_phantom.hpp"
#include "ics/camera/emulated_x6980.hpp"
#include "ics/camera/flir_camera.hpp"
#include "ics/camera/frame_meta.hpp"
#include "ics/camera/segment_camera.hpp"
#include "ics/camera/trigger_schedule.hpp"
#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/v1/camera_frame_meta.pb.h"
#include "ics/v1/time_quality.pb.h"
#include "scratch_dir.hpp"

namespace ics::camera {
namespace {

using std::chrono::microseconds;
using std::chrono::milliseconds;
using testing_support::ScratchDir;

constexpr std::int64_t kEpochNs = 1'791'331'200'000'000'000;

// 5,000 frames/s, 500 frames a segment, 400 of them after the trigger.
CameraSettings settings() {
  return CameraSettings{.width = 64,
                        .height = 32,
                        .window_x = 0,
                        .window_y = 0,
                        .frame_rate = 5000,
                        .exposure = microseconds(150),
                        .segment_frames = 500,
                        .post_trigger_frames = 400,
                        .irig = true};
}

// The station's time quality: IRIG-B locked or not, and the camera's
// measured offset.
class FixedQuality final : public TimeQualitySource {
 public:
  explicit FixedQuality(const bool locked) {
    quality_.set_time_utc_ns(kEpochNs);
    quality_.set_irig_b_locked(locked);
    v1::TimeQuality::CameraOffset* offset = quality_.add_camera_offsets();
    offset->set_camera_id("phantom-1");
    offset->set_offset_ns(250);
  }
  v1::TimeQuality current() override { return quality_; }

 private:
  v1::TimeQuality quality_;
};

OffloadPlan plan(const std::filesystem::path& directory) {
  return OffloadPlan{.station_id = "north",
                     .camera_id = "phantom-1",
                     .camera_kind = v1::CameraFrameMeta::CAMERA_KIND_HIGH_SPEED_VISIBLE,
                     .segments = 10,
                     .frames = FrameRange{.first = -50, .count = 300},
                     .directory = directory};
}

// Segments back to back: each trigger 120 ms after the one before, and each
// segment 100 ms long.
const TriggerSchedule kSchedule{.first = utc_from_ns(kEpochNs), .interval = milliseconds(120)};

TEST(Offload, TenBackToBackSegmentsOffloadWithVerifiedTimes) {
  const ScratchDir dir;
  EmulatedPhantom camera(kSchedule);
  FixedQuality quality(true);
  const Result<OffloadReport> report = record_and_offload(camera, settings(), plan(dir.path()), quality);
  ASSERT_TRUE(report.has_value());
  EXPECT_TRUE(report->verified);
  ASSERT_EQ(report->segments.size(), 10U);
  for (std::uint32_t s = 0; s < 10; ++s) {
    const SegmentCheck& saved = report->segments[s];
    EXPECT_TRUE(saved.verified) << s;
    EXPECT_EQ(saved.segment, s);
    EXPECT_EQ(saved.frames, 300U);
    EXPECT_LE(saved.spacing_error, Duration(1)) << s;
    EXPECT_LE(saved.trigger_error, Duration(1)) << s;
    EXPECT_EQ(saved.first, utc_from_ns(kEpochNs) + (milliseconds(120) * s) - milliseconds(10));
    EXPECT_TRUE(std::filesystem::exists(saved.path)) << s;
    ASSERT_EQ(saved.meta.size(), 300U);
    const v1::CameraFrameMeta& frame = saved.meta[50];
    EXPECT_EQ(frame.segment_id(), "cine-00" + std::to_string(s));
    EXPECT_EQ(frame.frame_index(), 50U);
    EXPECT_EQ(frame.time_source(), v1::CameraFrameMeta::TIME_SOURCE_IRIG);
    EXPECT_EQ(frame.camera_kind(), v1::CameraFrameMeta::CAMERA_KIND_HIGH_SPEED_VISIBLE);
    EXPECT_EQ(frame.exposure_start_utc_ns(), kEpochNs + (s * 120'000'000LL) - 250);
    EXPECT_EQ(frame.exposure_duration_ns(), 150'000);
  }
}

// The X6980: a 64 x 32 window at (288, 240), 1,004 frames/s, so a frame
// period of 996,015.9 ns, stamped to the microsecond without the year;
// triggers 600 ms apart, 0.3 µs past a microsecond.
TEST(Offload, TenBackToBackX6980SegmentsOffloadWithVerifiedTimes) {
  const ScratchDir dir;
  EmulatedX6980 sdk(TriggerSchedule{.first = utc_from_ns(kEpochNs + 300), .interval = milliseconds(600)}, false);
  FixedQuality quality(true);
  FlirCamera camera(sdk, quality);
  CameraSettings s = settings();
  s.window_x = 288;
  s.window_y = 240;
  s.frame_rate = 1004;
  s.exposure = microseconds(800);
  OffloadPlan p = plan(dir.path());
  p.camera_kind = v1::CameraFrameMeta::CAMERA_KIND_MWIR;
  const Result<OffloadReport> report = record_and_offload(camera, s, p, quality);
  ASSERT_TRUE(report.has_value());
  EXPECT_TRUE(report->verified);
  ASSERT_EQ(report->segments.size(), 10U);
  Duration worst{};
  for (const SegmentCheck& saved : report->segments) {
    EXPECT_TRUE(saved.verified) << saved.segment;
    EXPECT_EQ(saved.frames, 300U);
    worst = std::max(worst, saved.spacing_error);
    const v1::CameraFrameMeta& frame = saved.meta[50];
    EXPECT_EQ(frame.camera_kind(), v1::CameraFrameMeta::CAMERA_KIND_MWIR);
    EXPECT_EQ(frame.time_source(), v1::CameraFrameMeta::TIME_SOURCE_IRIG);
    EXPECT_EQ(frame.bits_per_pixel(), 14U);
    EXPECT_EQ(frame.window_x_px(), 288U);
    EXPECT_EQ(frame.window_y_px(), 240U);
    EXPECT_EQ(frame.exposure_duration_ns(), 800'000);
    EXPECT_EQ(frame.exposure_start_utc_ns(), kEpochNs + (saved.segment * 600'000'000LL) - 250);
  }
  // Microsecond stamps of a 996,015.9 ns period are 996 µs or 997 µs apart:
  // at most 984.1 ns off, within kSpacingTolerance.
  EXPECT_EQ(worst, Duration(984));
}

TEST(Offload, SegmentsNotTimedByIrigDoNotVerify) {
  for (const bool locked : {true, false}) {
    const ScratchDir dir;
    EmulatedPhantom camera(kSchedule);
    FixedQuality quality(locked);
    CameraSettings s = settings();
    s.irig = !locked;
    OffloadPlan p = plan(dir.path());
    p.segments = 2;
    const Result<OffloadReport> report = record_and_offload(camera, s, p, quality);
    ASSERT_TRUE(report.has_value()) << locked;
    EXPECT_FALSE(report->verified) << locked;
    EXPECT_FALSE(report->segments[0].irig) << locked;
    EXPECT_EQ(report->segments[0].meta[0].time_source(), v1::CameraFrameMeta::TIME_SOURCE_HOST) << locked;
  }
}

// A camera that fails at a given step, or saves a file that is not a cine.
class FailingCamera final : public SegmentCamera {
 public:
  enum class Step { kConfigure, kArm, kTrigger, kSave, kGarbage };
  explicit FailingCamera(const Step step) : step_(step) {}

  Result<CameraSettings> configure(const CameraSettings& s) override {
    return step_ == Step::kConfigure ? Result<CameraSettings>(fail(Error::kUnavailable)) : Result<CameraSettings>(s);
  }
  Status arm(std::uint32_t /*segments*/) override {
    return step_ == Step::kArm ? Status(fail(Error::kUnavailable)) : Status();
  }
  Result<SegmentStatus> trigger() override {
    return step_ == Step::kTrigger ? Result<SegmentStatus>(fail(Error::kUnavailable)) : Result<SegmentStatus>(SegmentStatus{});
  }
  Status save(std::uint32_t /*segment*/, const FrameRange& /*frames*/, const std::filesystem::path& path) override {
    std::ofstream(path, std::ios::binary) << "not a cine";
    return step_ == Step::kSave ? Status(fail(Error::kUnwritable)) : Status();
  }

 private:
  Step step_;
};

TEST(Offload, FailsWhereTheCameraFails) {
  const ScratchDir dir;
  FixedQuality quality(true);
  const std::pair<FailingCamera::Step, Error> cases[] = {{FailingCamera::Step::kConfigure, Error::kUnavailable},
                                                         {FailingCamera::Step::kArm, Error::kUnavailable},
                                                         {FailingCamera::Step::kTrigger, Error::kUnavailable},
                                                         {FailingCamera::Step::kSave, Error::kUnwritable},
                                                         {FailingCamera::Step::kGarbage, Error::kMalformed}};
  for (const auto& [step, error] : cases) {
    FailingCamera camera(step);
    EXPECT_EQ(record_and_offload(camera, settings(), plan(dir.path()), quality).error(), error)
        << static_cast<int>(step);
  }
  FailingCamera unreadable(FailingCamera::Step::kGarbage);
  OffloadPlan nowhere = plan(dir.path() / "missing");
  EXPECT_EQ(record_and_offload(unreadable, settings(), nowhere, quality).error(), Error::kUnreadable);
}

// A segment of 5 frames at 1,000 frames/s, from 2 frames before its trigger.
Cine segment() {
  Cine cine{.first_image_no = -2,
            .trigger_time = utc_from_ns(kEpochNs),
            .width = 8,
            .height = 4,
            .bit_count = 16,
            .real_bpp = 12,
            .frame_rate = 1000,
            .exposure = microseconds(500),
            .frame_times = {},
            .exposures = {},
            .image_offsets = {}};
  for (std::int64_t n = -2; n < 3; ++n) {
    cine.frame_times.push_back(utc_from_ns(kEpochNs) + milliseconds(n));
  }
  return cine;
}

SegmentExpectation expected() {
  return SegmentExpectation{.frames = FrameRange{.first = -2, .count = 5},
                            .trigger_time = utc_from_ns(kEpochNs),
                            .previous_last = utc_from_ns(kEpochNs) - milliseconds(3),
                            .authority = TimeAuthority{.irig_configured = true, .irig_locked = true, .camera_offset = {}}};
}

TEST(VerifySegment, HoldsASegmentToItsPlanItsTriggerAndItsFramePeriod) {
  EXPECT_TRUE(verify_segment(segment(), expected()).verified);
  SegmentExpectation fewer = expected();
  fewer.frames.count = 4;
  EXPECT_FALSE(verify_segment(segment(), fewer).verified);
  SegmentExpectation earlier = expected();
  earlier.frames.first = -3;
  EXPECT_FALSE(verify_segment(segment(), earlier).verified);
  SegmentExpectation overlapping = expected();
  overlapping.previous_last = utc_from_ns(kEpochNs) - milliseconds(2);
  EXPECT_FALSE(verify_segment(segment(), overlapping).ordered);
  SegmentExpectation first = expected();
  first.previous_last = std::nullopt;
  EXPECT_TRUE(verify_segment(segment(), first).ordered);
  Cine uneven = segment();
  uneven.frame_times[3] += microseconds(2);
  const SegmentCheck spacing = verify_segment(uneven, expected());
  EXPECT_EQ(spacing.spacing_error, microseconds(2));
  EXPECT_FALSE(spacing.verified);
  Cine late = segment();
  late.trigger_time += milliseconds(2);
  const SegmentCheck trigger = verify_segment(late, expected());
  EXPECT_EQ(trigger.trigger_error, milliseconds(2));
  EXPECT_FALSE(trigger.verified);
  Cine shifted = segment();
  std::ranges::transform(shifted.frame_times, shifted.frame_times.begin(),
                         [](const UtcTime t) { return t + milliseconds(2); });
  EXPECT_EQ(verify_segment(shifted, expected()).trigger_error, milliseconds(2));
}

TEST(VerifySegment, VerifiesNothingWithoutFramesOrAFrameRate) {
  Cine empty = segment();
  empty.frame_times.clear();
  EXPECT_FALSE(verify_segment(empty, expected()).verified);
  Cine still = segment();
  still.frame_rate = 0;
  EXPECT_FALSE(verify_segment(still, expected()).verified);
}

}  // namespace
}  // namespace ics::camera
