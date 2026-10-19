// camera_test.cpp: frame sizes, pixels, and which buffers make frames (ICS-038).
#include <array>
#include <chrono>
#include <cstddef>
#include <span>

#include <gtest/gtest.h>

#include <ics/common/error.hpp>
#include <ics/common/units.hpp>
#include <ics/common/universal_types.h>
#include <ics/tracker/aravis_camera.h>
#include <ics/tracker/camera.h>

namespace
{

using ics::int32;
using ics::uint64;
using ics::uint8;
using ics::tracker::Buffer_view;
using ics::tracker::Camera_settings;
using ics::tracker::Frame;
using ics::tracker::Pixel_format;

constexpr int32 width = 3;
constexpr int32 height = 2;
constexpr std::size_t mono16_bytes = 12U;
constexpr std::size_t oversize_bytes = 13U;
constexpr std::size_t low_at = 2U;
constexpr std::size_t high_at = 3U;
constexpr uint8 low_byte = 0x34U;
constexpr uint8 high_byte = 0x12U;
constexpr ics::float64 mono16_count = 4660.0;
constexpr ics::float64 mono8_count = 52.0;
constexpr uint64 stamp_ns = 1700000000123456789ULL;
constexpr ics::int64 offset_ns = 37000000000LL;
constexpr int32 slot = 3;
constexpr ics::int64 number = 7;
constexpr uint64 too_late_ns = 0x8000000000000000ULL;
constexpr ics::uint32 unknown_code = 0x010C0047U;
constexpr ics::float64 rate_hz = 100.0;
constexpr ics::float64 exposure_us = 1000.0;
constexpr int32 buffers = 4;
constexpr ics::float64 too_fast_hz = 20000.0;
constexpr ics::float64 too_long_us = 2.0E6;
constexpr int32 too_many_buffers = 33;
constexpr ics::int64 too_many_bytes = 1099511627776LL;

Camera_settings settings()
{
  return Camera_settings{"",
                         Pixel_format::mono16,
                         {0, 0, width, height},
                         rate_hz,
                         exposure_us,
                         buffers,
                         0,
                         ics::tracker::Stamp_source::camera,
                         std::chrono::nanoseconds(offset_ns)};
}

Buffer_view view_of(const std::span<const uint8> storage)
{
  return Buffer_view{0, storage.data(), storage.size(), width, height, ics::tracker::mono16_code, stamp_ns, 1U};
}

}  // namespace

// JSF deviation D-5: GoogleTest's TEST and EXPECT macros fix the form of the
// test names and expand to code of GoogleTest's own (AV 50, 51 and 209).
TEST(TrackerCamera, ReadsMono8AndLittleEndianMono16)
{
  std::array<uint8, mono16_bytes> bytes{};
  bytes[low_at] = low_byte;
  bytes[high_at] = high_byte;
  const Frame wide{1, ics::UtcTime{}, width, height, Pixel_format::mono16, bytes, 0};
  EXPECT_DOUBLE_EQ(ics::tracker::pixel_at(wide, 1, 0), mono16_count);
  const Frame narrow{1, ics::UtcTime{}, width, height, Pixel_format::mono8, bytes, 0};
  EXPECT_DOUBLE_EQ(ics::tracker::pixel_at(narrow, 2, 0), mono8_count);
  EXPECT_DOUBLE_EQ(ics::tracker::pixel_at(wide, width, 0), 0.0);
  EXPECT_DOUBLE_EQ(ics::tracker::pixel_at(wide, 0, height), 0.0);
  EXPECT_DOUBLE_EQ(ics::tracker::pixel_at(wide, -1, 0), 0.0);
  EXPECT_DOUBLE_EQ(ics::tracker::pixel_at(wide, 0, -1), 0.0);
  const Frame short_frame{1, ics::UtcTime{}, width, height, Pixel_format::mono16, std::span(bytes).first(2), 0};
  EXPECT_DOUBLE_EQ(ics::tracker::pixel_at(short_frame, 1, 0), 0.0);
  EXPECT_EQ(ics::tracker::bytes_needed(width, height, Pixel_format::mono16), 12);
}

TEST(TrackerCamera, MakesAFrameOfAFilledBuffer)
{
  std::array<uint8, mono16_bytes> storage{};
  const ics::Result<Frame> frame = ics::tracker::frame_of(view_of(storage), storage, slot, number, settings());
  ASSERT_TRUE(frame.has_value());
  EXPECT_EQ(frame->time.time_since_epoch().count(), static_cast<ics::int64>(stamp_ns) - offset_ns);
  EXPECT_EQ(frame->slot, slot);
  EXPECT_EQ(frame->number, number);
  EXPECT_EQ(frame->format, Pixel_format::mono16);
  EXPECT_EQ(frame->bytes.size(), mono16_bytes);
  Camera_settings host = settings();
  host.stamps = ics::tracker::Stamp_source::host;
  host.stamp_offset = ics::Duration::zero();
  const ics::Result<Frame> hosted = ics::tracker::frame_of(view_of(storage), storage, slot, number, host);
  ASSERT_TRUE(hosted.has_value());
  EXPECT_EQ(hosted->time.time_since_epoch().count(), 1);
  Buffer_view narrow = view_of(storage);
  narrow.pixel_format = ics::tracker::mono8_code;
  EXPECT_EQ(ics::tracker::frame_of(narrow, storage, slot, number, settings())->format, Pixel_format::mono8);
}

TEST(TrackerCamera, RefusesBuffersThatHoldNoUsableFrame)
{
  std::array<uint8, mono16_bytes> storage{};
  std::array<uint8, mono16_bytes> elsewhere{};
  const auto made = [&storage](const Buffer_view& view)
  {
    return ics::tracker::frame_of(view, storage, slot, number, settings()).error();
  };
  Buffer_view view = view_of(storage);
  view.status = 2;
  EXPECT_EQ(made(view), ics::Error::kUnavailable);
  view = view_of(storage);
  view.pixel_format = unknown_code;
  EXPECT_EQ(made(view), ics::Error::kMalformed);
  view = view_of(storage);
  view.width = 0;
  EXPECT_EQ(made(view), ics::Error::kMalformed);
  view = view_of(storage);
  view.height = 0;
  EXPECT_EQ(made(view), ics::Error::kMalformed);
  view = view_of(storage);
  view.data = elsewhere.data();
  EXPECT_EQ(made(view), ics::Error::kMalformed);
  view = view_of(storage);
  view.size = 2U;
  EXPECT_EQ(made(view), ics::Error::kMalformed);
  view = view_of(storage);
  view.size = oversize_bytes;
  EXPECT_EQ(made(view), ics::Error::kMalformed);
  view = view_of(storage);
  view.camera_ns = 0U;
  EXPECT_EQ(made(view), ics::Error::kUnconstrained);
  view.camera_ns = too_late_ns;
  EXPECT_EQ(made(view), ics::Error::kUnconstrained);
}

TEST(TrackerCamera, ChecksTheSettings)
{
  EXPECT_TRUE(ics::tracker::valid(settings()).has_value());
  const auto refused = [](const Camera_settings& changed)
  {
    return !ics::tracker::valid(changed).has_value();
  };
  Camera_settings changed = settings();
  changed.region.x = -1;
  EXPECT_TRUE(refused(changed));
  changed = settings();
  changed.region.y = -1;
  EXPECT_TRUE(refused(changed));
  changed = settings();
  changed.region.width = 0;
  EXPECT_TRUE(refused(changed));
  changed = settings();
  changed.region.height = 0;
  EXPECT_TRUE(refused(changed));
  changed = settings();
  changed.frame_rate_hz = 0.0;
  EXPECT_TRUE(refused(changed));
  changed.frame_rate_hz = too_fast_hz;
  EXPECT_TRUE(refused(changed));
  changed = settings();
  changed.exposure_us = 0.0;
  EXPECT_TRUE(refused(changed));
  changed.exposure_us = too_long_us;
  EXPECT_TRUE(refused(changed));
  changed = settings();
  changed.buffers = 1;
  EXPECT_TRUE(refused(changed));
  changed.buffers = too_many_buffers;
  EXPECT_TRUE(refused(changed));
  changed = settings();
  changed.buffer_bytes = -1;
  EXPECT_TRUE(refused(changed));
  changed.buffer_bytes = too_many_bytes;
  EXPECT_TRUE(refused(changed));
}
