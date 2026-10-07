#include "ics/strobe/detect.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "ics/camera/cine.hpp"
#include "ics/camera/strobe.hpp"
#include "ics/common/error.hpp"
#include "ics/common/units.hpp"

namespace ics::strobe {
namespace {

using std::chrono::microseconds;
using std::chrono::milliseconds;

constexpr std::int64_t kEpochNs = 1'791'331'200'000'000'000;

TEST(LitFrames, AreTheFramesWellAboveTheMedian) {
  // Noise of about 1 count: the lit frames stand far clear of it.
  const std::vector<double> segment{200, 201, 199, 200, 1200, 600, 200, 199, 201};
  EXPECT_EQ(lit_frames(segment), (std::vector<bool>{false, false, false, false, true, true, false, false, false}));
  // With no noise at all, a lift of one count is enough.
  EXPECT_EQ(lit_frames(std::vector<double>{5, 5, 6, 5}), (std::vector<bool>{false, false, true, false}));
  EXPECT_EQ(lit_frames(std::vector<double>{5, 5, 5.5, 5}), (std::vector<bool>{false, false, false, false}));
}

// Five frames at 1,000 frames/s, 4 x 2 pixels, from 2 frames before a
// trigger 0.6 s into a second: frame f's pixels all hold values[f].
camera::Cine sample(std::vector<std::byte>& bytes, const std::vector<std::uint16_t>& values) {
  camera::Cine cine{.first_image_no = -2,
                    .trigger_time = utc_from_ns(kEpochNs + 600'000'000),
                    .width = 4,
                    .height = 2,
                    .bit_count = 16,
                    .real_bpp = 12,
                    .frame_rate = 1000,
                    .exposure = microseconds(500),
                    .frame_times = {},
                    .exposures = {},
                    .image_offsets = {}};
  std::vector<std::byte> images;
  for (std::size_t f = 0; f < values.size(); ++f) {
    cine.frame_times.push_back(cine.trigger_time + milliseconds(static_cast<std::int64_t>(f) - 2));
    cine.exposures.push_back(microseconds(400 + static_cast<std::int64_t>(f)));
    for (std::size_t p = 0; p < 8; ++p) {
      images.push_back(static_cast<std::byte>(values[f] & 0xFFU));
      images.push_back(static_cast<std::byte>(values[f] >> 8U));
    }
  }
  bytes = camera::write_cine(cine, images).value();
  return camera::read_cine(bytes).value();
}

const camera::Roi kRoi{.x = 1, .y = 0, .width = 2, .height = 2};

TEST(ReadSegment, ReadsEachFramesBrightnessAndFindsTheLitOnes) {
  std::vector<std::byte> bytes;
  const camera::Cine cine = sample(bytes, {100, 100, 900, 400, 100});
  const Result<StrobeSegment> segment = read_segment(bytes, cine, kRoi);
  ASSERT_TRUE(segment.has_value());
  // The middle frame, 0.6 s into the second, is nearest the next second.
  EXPECT_EQ(segment->second, (kEpochNs / 1'000'000'000) + 1);
  ASSERT_EQ(segment->frames.size(), 5U);
  EXPECT_EQ(segment->frames[2].brightness, 900.0);
  EXPECT_EQ(segment->frames[2].stamp, cine.frame_times[2]);
  EXPECT_EQ(segment->frames[2].exposure, microseconds(402));
  EXPECT_EQ(segment->lit, 2U);
  EXPECT_TRUE(segment->contiguous);
  // Without exposures of its own, a frame takes the setup's.
  camera::Cine plain = cine;
  plain.exposures.clear();
  EXPECT_EQ(read_segment(bytes, plain, kRoi).value().frames[2].exposure, microseconds(500));
}

TEST(ReadSegment, TellsLitFramesApartAndNoneLit) {
  std::vector<std::byte> bytes;
  const camera::Cine scattered = sample(bytes, {900, 100, 100, 100, 900});
  const Result<StrobeSegment> apart = read_segment(bytes, scattered, kRoi);
  ASSERT_TRUE(apart.has_value());
  EXPECT_EQ(apart->lit, 2U);
  EXPECT_FALSE(apart->contiguous);
  const camera::Cine dark = sample(bytes, {100, 100, 100, 100, 100});
  EXPECT_EQ(read_segment(bytes, dark, kRoi).value().lit, 0U);
}

TEST(ReadSegment, RefusesWhatItCannotRead) {
  std::vector<std::byte> bytes;
  const camera::Cine two = sample(bytes, {100, 900});
  EXPECT_EQ(read_segment(bytes, two, kRoi).error(), Error::kEmpty);
  const camera::Cine five = sample(bytes, {100, 100, 900, 100, 100});
  EXPECT_EQ(read_segment(bytes, five, camera::Roi{.x = 3, .y = 0, .width = 2, .height = 1}).error(),
            Error::kOutOfRange);
}

}  // namespace
}  // namespace ics::strobe
