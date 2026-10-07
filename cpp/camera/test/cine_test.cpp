#include "ics/camera/cine.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"

namespace ics::camera {
namespace {

using std::chrono::microseconds;
using std::chrono::milliseconds;
using std::chrono::seconds;

// 2026-10-07T00:00:00Z.
constexpr std::int64_t kEpochNs = 1'791'331'200'000'000'000;

// Where the writer puts each part, as read_cine finds them.
constexpr std::size_t kBitmapAt = 44;
constexpr std::size_t kSetupAt = 84;
constexpr std::size_t kTagsAt = kSetupAt + 7240;

// A segment of a 1,000 frame/s camera, saved from two frames before its
// trigger, with a trigger 0.25 ms into its frame.
Cine sample(const std::size_t frames, const bool exposures) {
  Cine cine{.first_image_no = -2,
            .trigger_time = utc_from_ns(kEpochNs) + microseconds(250),
            .width = 8,
            .height = 4,
            .bit_count = 16,
            .real_bpp = 12,
            .frame_rate = 1000,
            .exposure = microseconds(500),
            .frame_times = {},
            .exposures = {},
            .image_offsets = {}};
  for (std::size_t i = 0; i < frames; ++i) {
    cine.frame_times.push_back(utc_from_ns(kEpochNs) + milliseconds(static_cast<std::int64_t>(i) - 2) + Duration(7));
    if (exposures) {
      cine.exposures.push_back(microseconds(400) + Duration(static_cast<std::int64_t>(i)));
    }
  }
  return cine;
}

std::vector<std::byte> written(const Cine& cine) { return write_cine(cine).value(); }

template <typename T>
void put(std::vector<std::byte>& bytes, const std::size_t at, const T value) {
  std::memcpy(std::span(bytes).subspan(at, sizeof value).data(), &value, sizeof value);
}

template <typename T>
T get(const std::vector<std::byte>& bytes, const std::size_t at) {
  T value{};
  std::memcpy(&value, std::span(bytes).subspan(at, sizeof value).data(), sizeof value);
  return value;
}

TEST(Time64, KeepsWholeSecondsAndABinaryFraction) {
  EXPECT_EQ(from_time64((std::uint64_t{1} << 32U) | (std::uint64_t{1} << 31U)), utc_from_ns(1'500'000'000));
  EXPECT_EQ(from_time64(0), utc_from_ns(0));
  for (const std::int64_t ns : {std::int64_t{0}, kEpochNs + 1, kEpochNs + 999'999'999, kEpochNs + 123'456'789}) {
    EXPECT_EQ(from_time64(to_time64(utc_from_ns(ns)).value()), utc_from_ns(ns)) << ns;
  }
  constexpr std::int64_t kLastSecondNs = std::int64_t{std::numeric_limits<std::uint32_t>::max()} * 1'000'000'000;
  EXPECT_TRUE(to_time64(utc_from_ns(kLastSecondNs + 999'999'999)).has_value());
  EXPECT_EQ(to_time64(utc_from_ns(kLastSecondNs + 1'000'000'000)), std::nullopt);
  EXPECT_EQ(to_time64(utc_from_ns(-1)), std::nullopt);
}

TEST(Cine, ReadsWhatItWrites) {
  for (const bool exposures : {true, false}) {
    const Cine cine = sample(5, exposures);
    const std::vector<std::byte> bytes = written(cine);
    const Result<Cine> read = read_cine(bytes);
    ASSERT_TRUE(read.has_value()) << exposures;
    EXPECT_EQ(read->first_image_no, -2);
    EXPECT_EQ(read->trigger_time, cine.trigger_time);
    EXPECT_EQ(read->width, 8U);
    EXPECT_EQ(read->height, 4U);
    EXPECT_EQ(read->bit_count, 16U);
    EXPECT_EQ(read->real_bpp, 12U);
    EXPECT_EQ(read->frame_rate, 1000U);
    EXPECT_EQ(read->exposure, microseconds(500));
    EXPECT_EQ(read->frame_times, cine.frame_times);
    EXPECT_EQ(read->exposures, cine.exposures);
    ASSERT_EQ(read->image_offsets.size(), 5U);
    // Each image block: an 8-byte annotation, then 8 x 4 pixels of 2 bytes.
    EXPECT_EQ(read->image_offsets[1] - read->image_offsets[0], 8U + 64U);
    EXPECT_EQ(read->image_offsets.back() + 8U + 64U, bytes.size());
  }
}

TEST(Cine, WritesOnlyWhatItCanRead) {
  const std::vector<std::pair<std::string, std::function<void(Cine&)>>> refused{
      {"no width", [](Cine& c) { c.width = 0; }},
      {"too wide", [](Cine& c) { c.width = 65'536; }},
      {"no height", [](Cine& c) { c.height = 0; }},
      {"too high", [](Cine& c) { c.height = 65'536; }},
      {"part bytes", [](Cine& c) { c.bit_count = 12; }},
      {"no bits", [](Cine& c) { c.bit_count = 0; }},
      {"too many pixels", [](Cine& c) { c.width = c.height = 65'535; c.bit_count = 16; }},
      {"no frame rate", [](Cine& c) { c.frame_rate = 0; }},
      {"no frames", [](Cine& c) { c.frame_times.clear(); c.exposures.clear(); }},
      {"too many frames", [](Cine& c) { c.frame_times.resize((std::size_t{1} << 20U) + 1, c.trigger_time); c.exposures.clear(); }},
      {"an exposure short", [](Cine& c) { c.exposures.pop_back(); }},
      {"negative shutter", [](Cine& c) { c.exposure = Duration(-1); }},
      {"long shutter", [](Cine& c) { c.exposure = Duration(std::int64_t{1} << 32U); }},
      {"negative exposure", [](Cine& c) { c.exposures[1] = Duration(-1); }},
      {"exposure of a second", [](Cine& c) { c.exposures[1] = seconds(1); }},
      {"trigger before 1970", [](Cine& c) { c.trigger_time = utc_from_ns(-1); }},
      {"frame before 1970", [](Cine& c) { c.frame_times[1] = utc_from_ns(-1); }},
      {"over a gibibyte", [](Cine& c) { c.width = c.height = 4096; c.frame_times.resize(40, c.trigger_time); c.exposures.clear(); }},
  };
  for (const auto& [name, change] : refused) {
    Cine cine = sample(3, true);
    change(cine);
    EXPECT_EQ(write_cine(cine).error(), Error::kInvalidArgument) << name;
  }
}

TEST(Cine, WritesTheImagesGiven) {
  const Cine cine = sample(2, false);
  // 8 x 4 pixels of 2 bytes a frame.
  std::vector<std::byte> images(2 * 64);
  images[0] = std::byte{0xA1};
  images[64] = std::byte{0xB2};
  const std::vector<std::byte> bytes = write_cine(cine, images).value();
  const Cine read = read_cine(bytes).value();
  EXPECT_EQ(bytes[read.image_offsets[0] + 8], std::byte{0xA1});
  EXPECT_EQ(bytes[read.image_offsets[1] + 8], std::byte{0xB2});
  images.pop_back();
  EXPECT_EQ(write_cine(cine, images).error(), Error::kInvalidArgument);
}

TEST(Cine, RefusesWhatDoesNotFollowTheLayout) {
  const std::vector<std::byte> good = written(sample(3, true));
  const std::size_t exposures_at = kTagsAt + 8 + (3 * 8);
  const std::size_t offsets_at = exposures_at + 8 + (3 * 4);
  const auto first_image = static_cast<std::size_t>(get<std::uint64_t>(good, offsets_at));
  const std::vector<std::pair<std::string, std::function<void(std::vector<std::byte>&)>>> refused{
      {"short header", [](std::vector<std::byte>& b) { b.resize(43); }},
      {"not CI", [](std::vector<std::byte>& b) { put<std::uint16_t>(b, 0, 0x4943 + 1); }},
      {"header size", [](std::vector<std::byte>& b) { put<std::uint16_t>(b, 2, 43); }},
      {"bitmap past end", [](std::vector<std::byte>& b) { put<std::uint32_t>(b, 24, static_cast<std::uint32_t>(b.size())); }},
      {"no width", [](std::vector<std::byte>& b) { put<std::int32_t>(b, kBitmapAt + 4, 0); }},
      {"negative height", [](std::vector<std::byte>& b) { put<std::int32_t>(b, kBitmapAt + 8, -4); }},
      {"no bits", [](std::vector<std::byte>& b) { put<std::uint16_t>(b, kBitmapAt + 14, 0); }},
      {"setup past end", [](std::vector<std::byte>& b) { put<std::uint32_t>(b, 28, static_cast<std::uint32_t>(b.size())); }},
      {"not ST", [](std::vector<std::byte>& b) { put<std::uint16_t>(b, kSetupAt + 140, 0); }},
      {"short setup", [](std::vector<std::byte>& b) { put<std::uint16_t>(b, kSetupAt + 142, 1571); }},
      {"setup runs past end", [](std::vector<std::byte>& b) { b.resize(kSetupAt + 2000); }},
      {"no frame rate", [](std::vector<std::byte>& b) { put<std::uint32_t>(b, kSetupAt + 768, 0); }},
      {"offsets past end", [](std::vector<std::byte>& b) { put<std::uint32_t>(b, 32, static_cast<std::uint32_t>(b.size() - 8)); }},
      {"tags after offsets", [](std::vector<std::byte>& b) { put<std::uint32_t>(b, 32, kTagsAt - 8); }},
      {"short block", [](std::vector<std::byte>& b) { put<std::uint32_t>(b, kTagsAt, 7); }},
      {"block past offsets", [](std::vector<std::byte>& b) { put<std::uint32_t>(b, kTagsAt, 8 + (3 * 8) + 8 + (3 * 4) + 1); }},
      {"times short", [](std::vector<std::byte>& b) { put<std::uint32_t>(b, kTagsAt, 8 + (2 * 8)); }},
      {"exposures short", [=](std::vector<std::byte>& b) { put<std::uint32_t>(b, exposures_at, 8 + (2 * 4)); }},
      {"no times", [](std::vector<std::byte>& b) { put<std::uint16_t>(b, kTagsAt + 4, 1001); put<std::uint16_t>(b, kTagsAt + 6, 0); }},
      {"range first", [](std::vector<std::byte>& b) { put<std::uint16_t>(b, kTagsAt + 4, 1004); }},
      {"image past end", [=](std::vector<std::byte>& b) { put<std::uint64_t>(b, offsets_at, b.size()); }},
      {"short annotation", [=](std::vector<std::byte>& b) { put<std::uint32_t>(b, first_image, 7); }},
      {"annotation past end", [=](std::vector<std::byte>& b) { put<std::uint32_t>(b, first_image, static_cast<std::uint32_t>(b.size())); }},
      {"no image", [=](std::vector<std::byte>& b) { put<std::uint32_t>(b, first_image + 4, 0); }},
      {"image runs past end", [](std::vector<std::byte>& b) { b.pop_back(); }},
  };
  for (const auto& [name, change] : refused) {
    std::vector<std::byte> bytes = good;
    change(bytes);
    EXPECT_EQ(read_cine(bytes).error(), Error::kMalformed) << name;
  }
  std::vector<std::byte> none = good;
  put<std::uint32_t>(none, 20, 0);
  EXPECT_EQ(read_cine(none).error(), Error::kEmpty);
}

TEST(Cine, ReadsOnlyTheBlocksItKnowsAndStopsWhereTheyEnd) {
  const std::vector<std::byte> good = written(sample(3, true));
  const std::size_t exposures_at = kTagsAt + 8 + (3 * 8);
  std::vector<std::byte> other = good;
  put<std::uint16_t>(other, exposures_at + 4, 1001);
  const Result<Cine> skipped = read_cine(other);
  ASSERT_TRUE(skipped.has_value());
  EXPECT_TRUE(skipped->exposures.empty());
  EXPECT_EQ(skipped->frame_times.size(), 3U);
  std::vector<std::byte> last = good;
  put<std::uint16_t>(last, kTagsAt + 6, 0);
  const Result<Cine> stopped = read_cine(last);
  ASSERT_TRUE(stopped.has_value());
  EXPECT_TRUE(stopped->exposures.empty());
  // A last block that says another follows ends where the offsets start.
  std::vector<std::byte> more = good;
  put<std::uint16_t>(more, exposures_at + 6, 1);
  const Result<Cine> ended = read_cine(more);
  ASSERT_TRUE(ended.has_value());
  EXPECT_EQ(ended->exposures.size(), 3U);
}

}  // namespace
}  // namespace ics::camera
