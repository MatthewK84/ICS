#include "ics/camera/frame_meta.hpp"

#include <chrono>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "ics/camera/cine.hpp"
#include "ics/common/units.hpp"
#include "ics/v1/camera_frame_meta.pb.h"
#include "ics/v1/time_quality.pb.h"

namespace ics::camera {
namespace {

using std::chrono::microseconds;

constexpr std::int64_t kEpochNs = 1'791'331'200'000'000'000;

v1::TimeQuality quality(const bool locked) {
  v1::TimeQuality out;
  out.set_irig_b_locked(locked);
  const auto add = [&out](const char* camera, const std::int64_t offset_ns, const std::int64_t measured_ns) {
    v1::TimeQuality::CameraOffset* offset = out.add_camera_offsets();
    offset->set_camera_id(camera);
    offset->set_offset_ns(offset_ns);
    offset->set_measured_utc_ns(measured_ns);
  };
  add("phantom-1", 300, kEpochNs + 20);
  add("phantom-2", 999, kEpochNs + 50);
  add("phantom-1", 250, kEpochNs + 30);
  add("phantom-1", 100, kEpochNs + 10);
  return out;
}

Cine two_frames() {
  return Cine{.first_image_no = 0,
              .trigger_time = utc_from_ns(kEpochNs),
              .width = 64,
              .height = 32,
              .bit_count = 16,
              .real_bpp = 12,
              .frame_rate = 5000,
              .exposure = microseconds(150),
              .frame_times = {utc_from_ns(kEpochNs), utc_from_ns(kEpochNs + 200'000)},
              .exposures = {microseconds(140)},
              .image_offsets = {}};
}

TEST(TimeAuthority, TakesTheCamerasLatestOffsetAndBothSourcesOfIrig) {
  const TimeAuthority locked = time_authority(true, quality(true), "phantom-1");
  EXPECT_EQ(locked.camera_offset, Duration(250));
  EXPECT_TRUE(locked.irig());
  EXPECT_EQ(time_authority(true, quality(true), "phantom-9").camera_offset, Duration(0));
  EXPECT_FALSE(time_authority(true, quality(false), "phantom-1").irig());
  EXPECT_FALSE(time_authority(false, quality(true), "phantom-1").irig());
}

TEST(FrameMeta, TimesEachFrameLessTheCameraOffset) {
  const SegmentSource source{.station_id = "north",
                             .camera_id = "phantom-1",
                             .camera_kind = v1::CameraFrameMeta::CAMERA_KIND_HIGH_SPEED_VISIBLE,
                             .segment_id = "cine-004",
                             .window_x = 320,
                             .window_y = 256};
  const std::vector<v1::CameraFrameMeta> frames =
      frame_meta(two_frames(), source, time_authority(true, quality(true), "phantom-1"));
  ASSERT_EQ(frames.size(), 2U);
  EXPECT_EQ(frames[1].station_id(), "north");
  EXPECT_EQ(frames[1].camera_id(), "phantom-1");
  EXPECT_EQ(frames[1].camera_kind(), v1::CameraFrameMeta::CAMERA_KIND_HIGH_SPEED_VISIBLE);
  EXPECT_EQ(frames[1].segment_id(), "cine-004");
  EXPECT_EQ(frames[1].frame_index(), 1U);
  EXPECT_EQ(frames[1].exposure_start_utc_ns(), kEpochNs + 200'000 - 250);
  EXPECT_EQ(frames[1].time_offset_applied_ns(), 250);
  EXPECT_EQ(frames[1].time_source(), v1::CameraFrameMeta::TIME_SOURCE_IRIG);
  EXPECT_EQ(frames[1].width_px(), 64U);
  EXPECT_EQ(frames[1].height_px(), 32U);
  EXPECT_EQ(frames[1].bits_per_pixel(), 12U);
  EXPECT_EQ(frames[1].window_x_px(), 320U);
  EXPECT_EQ(frames[1].window_y_px(), 256U);
  // A frame's own exposure when the cine has it, else the setup's.
  EXPECT_EQ(frames[0].exposure_duration_ns(), 140'000);
  EXPECT_EQ(frames[1].exposure_duration_ns(), 150'000);
}

TEST(FrameMeta, MarksFramesNotTimedByIrigAsHostTimed) {
  Cine cine = two_frames();
  cine.real_bpp = 0;
  const std::vector<v1::CameraFrameMeta> frames = frame_meta(cine, SegmentSource{}, time_authority(true, quality(false), "x"));
  EXPECT_EQ(frames[0].time_source(), v1::CameraFrameMeta::TIME_SOURCE_HOST);
  EXPECT_EQ(frames[0].bits_per_pixel(), 16U);
  EXPECT_EQ(frames[0].exposure_start_utc_ns(), kEpochNs);
}

}  // namespace
}  // namespace ics::camera
