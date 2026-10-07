#include "ics/camera/image.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include "ics/camera/cine.hpp"
#include "ics/camera/strobe.hpp"
#include "ics/common/error.hpp"
#include "ics/common/units.hpp"

namespace ics::camera {
namespace {

constexpr std::int64_t kEpochNs = 1'791'331'200'000'000'000;

// Two frames of 4 x 3 pixels of bit_count bits.
Cine sample(const std::uint16_t bit_count) {
  return Cine{.first_image_no = 0,
              .trigger_time = utc_from_ns(kEpochNs),
              .width = 4,
              .height = 3,
              .bit_count = bit_count,
              .real_bpp = bit_count,
              .frame_rate = 1000,
              .exposure = std::chrono::microseconds(500),
              .frame_times = {utc_from_ns(kEpochNs), utc_from_ns(kEpochNs + 1'000'000)},
              .exposures = {},
              .image_offsets = {}};
}

// Frame f's pixel (column, row) holds 100 f + 10 row + column.
std::vector<std::byte> images(const std::size_t depth) {
  std::vector<std::byte> out;
  for (std::uint32_t f = 0; f < 2; ++f) {
    for (std::uint32_t row = 0; row < 3; ++row) {
      for (std::uint32_t column = 0; column < 4; ++column) {
        const std::uint32_t value = (100 * f) + (10 * row) + column;
        out.push_back(static_cast<std::byte>(value & 0xFFU));
        if (depth == 2) {
          out.push_back(static_cast<std::byte>(value >> 8U));
        }
      }
    }
  }
  return out;
}

TEST(RoiMean, AveragesTheRectangleOfAFrame) {
  for (const std::uint16_t bits : {std::uint16_t{8}, std::uint16_t{16}}) {
    const std::vector<std::byte> bytes = write_cine(sample(bits), images(bits / 8U)).value();
    const Cine cine = read_cine(bytes).value();
    // Frame 1, columns 1 and 2 of rows 1 and 2: 111, 112, 121, 122.
    EXPECT_EQ(roi_mean(bytes, cine, 1, Roi{.x = 1, .y = 1, .width = 2, .height = 2}).value(), 116.5) << bits;
    EXPECT_EQ(roi_mean(bytes, cine, 0, Roi{.x = 3, .y = 2, .width = 1, .height = 1}).value(), 23.0) << bits;
  }
}

TEST(RoiMean, RefusesWhatItCannotRead) {
  std::vector<std::byte> bytes = write_cine(sample(16), images(2)).value();
  const Cine cine = read_cine(bytes).value();
  const Roi all{.x = 0, .y = 0, .width = 4, .height = 3};
  EXPECT_EQ(roi_mean(bytes, cine, 2, all).error(), Error::kOutOfRange);
  EXPECT_EQ(roi_mean(bytes, cine, 0, Roi{.x = 1, .y = 0, .width = 4, .height = 3}).error(), Error::kOutOfRange);
  EXPECT_EQ(roi_mean(bytes, cine, 0, Roi{.x = 0, .y = 1, .width = 4, .height = 3}).error(), Error::kOutOfRange);
  EXPECT_EQ(roi_mean(bytes, cine, 0, Roi{.x = 0, .y = 0, .width = 0, .height = 3}).error(), Error::kInvalidArgument);
  EXPECT_EQ(roi_mean(bytes, cine, 0, Roi{.x = 0, .y = 0, .width = 4, .height = 0}).error(), Error::kInvalidArgument);
  Cine packed = cine;
  packed.bit_count = 12;
  EXPECT_EQ(roi_mean(bytes, packed, 0, all).error(), Error::kMalformed);
  // An image size in the annotation smaller than the image.
  const auto at = static_cast<std::size_t>(cine.image_offsets[0]);
  const std::uint32_t small = 23;
  std::memcpy(std::span(bytes).subspan(at + 4, 4).data(), &small, 4);
  EXPECT_EQ(roi_mean(bytes, cine, 0, all).error(), Error::kMalformed);
  // An annotation too short, past the end, or an image past the end.
  Cine moved = cine;
  moved.image_offsets[1] = bytes.size() - 4;
  EXPECT_EQ(roi_mean(bytes, moved, 1, all).error(), Error::kMalformed);
  const std::vector<std::byte> cut(bytes.begin(), bytes.end() - 1);
  EXPECT_EQ(roi_mean(cut, cine, 1, all).error(), Error::kMalformed);
  std::vector<std::byte> long_annotation = bytes;
  const std::uint32_t huge = 0xFFFF'FF00U;
  std::memcpy(std::span(long_annotation).subspan(static_cast<std::size_t>(cine.image_offsets[1]), 4).data(), &huge, 4);
  EXPECT_EQ(roi_mean(long_annotation, cine, 1, all).error(), Error::kMalformed);
  std::vector<std::byte> short_annotation = bytes;
  const std::uint32_t seven = 7;
  std::memcpy(std::span(short_annotation).subspan(static_cast<std::size_t>(cine.image_offsets[1]), 4).data(), &seven, 4);
  EXPECT_EQ(roi_mean(short_annotation, cine, 1, all).error(), Error::kMalformed);
}

}  // namespace
}  // namespace ics::camera
