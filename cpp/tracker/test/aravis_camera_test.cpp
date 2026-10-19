// aravis_camera_test.cpp: the Aravis binding, against Aravis's fake camera (ICS-038).
#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>

#include <gtest/gtest.h>

#include <ics/common/error.hpp>
#include <ics/common/units.hpp>
#include <ics/common/universal_types.h>
#include <ics/tracker/aravis_camera.h>
#include <ics/tracker/camera.h>

namespace
{

using ics::int32;
using ics::tracker::Aravis_camera;
using ics::tracker::Camera_settings;
using ics::tracker::Frame;
using ics::tracker::Pixel_format;

constexpr int32 width = 64;
constexpr int32 height = 48;
constexpr ics::float64 rate_hz = 100.0;
constexpr ics::float64 slow_hz = 1.0;
constexpr ics::float64 exposure_us = 1000.0;
constexpr int32 buffers = 4;
constexpr int32 frames = 5;
constexpr int32 attempts = 10;
constexpr ics::int64 tiny_buffer = 16;
constexpr ics::Duration frame_wait = std::chrono::seconds(2);
constexpr ics::Duration brief_wait = std::chrono::milliseconds(1);
constexpr ics::Duration clock_slack = std::chrono::seconds(60);
constexpr int32 bad_slot = 99;

Camera_settings settings(const Pixel_format format)
{
  return Camera_settings{"",
                         format,
                         {0, 0, width, height},
                         rate_hz,
                         exposure_us,
                         buffers,
                         0,
                         ics::tracker::Stamp_source::camera,
                         ics::Duration::zero()};
}

std::unique_ptr<Aravis_camera> fake()
{
  ics::Result<std::unique_ptr<Aravis_camera>> opened = Aravis_camera::open("Fake_1", true);
  EXPECT_TRUE(opened.has_value());
  return std::move(*opened);
}

}  // namespace

// JSF deviation D-5: GoogleTest's TEST and EXPECT macros fix the form of the
// test names and expand to code of GoogleTest's own (AV 50, 51 and 209).
TEST(TrackerAravis, OpensOnlyACameraThatAnswers)
{
  EXPECT_EQ(Aravis_camera::open("", true).error(), ics::Error::kInvalidArgument);
  EXPECT_EQ(Aravis_camera::open("No-Such-Camera", false).error(), ics::Error::kUnavailable);
}

TEST(TrackerAravis, StreamsTheFakeCamerasFrames)
{
  std::unique_ptr<Aravis_camera> camera = fake();
  ASSERT_TRUE(camera->start(settings(Pixel_format::mono16)).has_value());
  const ics::UtcTime started = std::chrono::time_point_cast<ics::Duration>(std::chrono::system_clock::now());
  for (int32 count = 1; count <= frames; ++count)
  {
    const ics::Result<Frame> frame = camera->next(frame_wait);
    ASSERT_TRUE(frame.has_value()) << ics::to_string(frame.error());
    EXPECT_EQ(frame->number, static_cast<ics::uint64>(count));
    EXPECT_EQ(frame->width, width);
    EXPECT_EQ(frame->height, height);
    EXPECT_EQ(frame->format, Pixel_format::mono16);
    EXPECT_LT(std::chrono::abs(frame->time - started), clock_slack);
    EXPECT_TRUE(camera->release(*frame).has_value());
  }
  EXPECT_EQ(camera->dropped(), 0U);
  EXPECT_TRUE(camera->stop().has_value());
  EXPECT_TRUE(camera->stop().has_value());
}

TEST(TrackerAravis, StreamsMono8WithHostStamps)
{
  std::unique_ptr<Aravis_camera> camera = fake();
  Camera_settings host = settings(Pixel_format::mono8);
  host.stamps = ics::tracker::Stamp_source::host;
  ASSERT_TRUE(camera->start(host).has_value());
  const ics::Result<Frame> frame = camera->next(frame_wait);
  ASSERT_TRUE(frame.has_value());
  EXPECT_EQ(frame->format, Pixel_format::mono8);
  EXPECT_EQ(frame->bytes.size(), static_cast<std::size_t>(width * height));
  EXPECT_TRUE(camera->release(*frame).has_value());
}

TEST(TrackerAravis, RefusesCallsOutOfTurn)
{
  std::unique_ptr<Aravis_camera> camera = fake();
  EXPECT_EQ(camera->next(frame_wait).error(), ics::Error::kInvalidArgument);
  const Frame stranger{1U, ics::UtcTime{}, width, height, Pixel_format::mono8, {}, 0};
  EXPECT_EQ(camera->release(stranger).error(), ics::Error::kInvalidArgument);
  Camera_settings bad = settings(Pixel_format::mono8);
  bad.buffers = 0;
  EXPECT_EQ(camera->start(bad).error(), ics::Error::kInvalidArgument);
  ASSERT_TRUE(camera->start(settings(Pixel_format::mono8)).has_value());
  EXPECT_EQ(camera->start(settings(Pixel_format::mono8)).error(), ics::Error::kInvalidArgument);
  Frame foreign = stranger;
  foreign.slot = bad_slot;
  EXPECT_EQ(camera->release(foreign).error(), ics::Error::kInvalidArgument);
  foreign.slot = -1;
  EXPECT_EQ(camera->release(foreign).error(), ics::Error::kInvalidArgument);
}

TEST(TrackerAravis, ReportsAFeatureTheCameraRefuses)
{
  std::unique_ptr<Aravis_camera> camera = fake();
  Camera_settings odd = settings(Pixel_format::mono16);
  odd.features = "NoSuchFeature=1";
  EXPECT_EQ(camera->start(odd).error(), ics::Error::kUnavailable);
  EXPECT_FALSE(camera->last_error().empty());
  EXPECT_EQ(camera->next(frame_wait).error(), ics::Error::kInvalidArgument);
}

TEST(TrackerAravis, DropsBuffersTooSmallForAFrame)
{
  std::unique_ptr<Aravis_camera> camera = fake();
  Camera_settings small = settings(Pixel_format::mono16);
  small.buffer_bytes = tiny_buffer;
  ASSERT_TRUE(camera->start(small).has_value());
  EXPECT_EQ(camera->next(frame_wait).error(), ics::Error::kUnavailable);
  EXPECT_GE(camera->dropped(), 1U);
}

TEST(TrackerAravis, WaitsNoLongerThanAsked)
{
  std::unique_ptr<Aravis_camera> camera = fake();
  Camera_settings slow = settings(Pixel_format::mono16);
  slow.frame_rate_hz = slow_hz;
  ASSERT_TRUE(camera->start(slow).has_value());
  bool waited = false;
  for (int32 attempt = 0; attempt < attempts; ++attempt)
  {
    const ics::Result<Frame> frame = camera->next(brief_wait);
    waited = waited || (!frame.has_value() && (frame.error() == ics::Error::kEmpty));
    if (frame.has_value())
    {
      EXPECT_TRUE(camera->release(*frame).has_value());
    }
  }
  EXPECT_TRUE(waited);
}
