#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <vector>

#include "bytes.hpp"
#include "ics/camera/cine.hpp"
#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "layout.hpp"

namespace ics::camera {
namespace {

using detail::kAnnotationMinimum;
using detail::kBitmapBitCountAt;
using detail::kBitmapBytes;
using detail::kBitmapHeightAt;
using detail::kBitmapImageSizeAt;
using detail::kBitmapPlanesAt;
using detail::kBitmapWidthAt;
using detail::kCineMark;
using detail::kExposureBlock;
using detail::kFirstImageNoAt;
using detail::kFirstMovieImageAt;
using detail::kFractionBits;
using detail::kFrameRateAt;
using detail::kHeaderBytes;
using detail::kHeaderSizeAt;
using detail::kImageCountAt;
using detail::kNsPerSecond;
using detail::kOffImageHeaderAt;
using detail::kOffImageOffsetsAt;
using detail::kOffSetupAt;
using detail::kRealBppAt;
using detail::kSetupLengthAt;
using detail::kSetupMark;
using detail::kSetupMarkAt;
using detail::kShutterNsAt;
using detail::kSoftwareVersionAt;
using detail::kTagHeaderBytes;
using detail::kTagMoreAt;
using detail::kTagTypeAt;
using detail::kTimeBlock;
using detail::kTotalImageCountAt;
using detail::kTriggerTimeAt;
using detail::kVersionAt;
using detail::put_i32;
using detail::put_u16;
using detail::put_u32;
using detail::put_u64;
using detail::store;

// The whole setup as PIMS lays it out, which it reads as software version
// 702 lays it out, so every field it reads lies within it.
constexpr std::size_t kSetupBytes = 7240;
constexpr std::uint32_t kSoftwareVersion = 702;
constexpr std::uint16_t kHeaderVersion = 1;
constexpr std::uint16_t kBitsPerByte = 8;
constexpr std::uint32_t kMaxSide = 65'535;
constexpr std::size_t kMaxFrames = std::size_t{1} << 20U;
constexpr std::size_t kMaxBytes = std::size_t{1} << 30U;
constexpr Duration kMaxShutter{std::numeric_limits<std::uint32_t>::max()};
// A frame's exposure is a binary fraction of a second.
constexpr Duration kMaxFrameExposure = std::chrono::seconds(1) - Duration(1);

// Where each part of the file starts, and its size.
struct Layout {
  std::size_t bitmap = 0;
  std::size_t setup = 0;
  std::size_t tags = 0;
  std::size_t offsets = 0;
  std::size_t images = 0;
  std::size_t pixels = 0;
  std::size_t total = 0;
};

[[nodiscard]] bool within(const Duration value, const Duration most) noexcept {
  return std::clamp(value, Duration::zero(), most) == value;
}

[[nodiscard]] bool writable(const Cine& cine) {
  const std::size_t frames = cine.frame_times.size();
  const std::uint64_t pixels = std::uint64_t{cine.width} * cine.height * (cine.bit_count / kBitsPerByte);
  const bool shape = std::clamp(cine.width, 1U, kMaxSide) == cine.width &&
                     std::clamp(cine.height, 1U, kMaxSide) == cine.height && cine.bit_count % kBitsPerByte == 0 &&
                     pixels > 0 && pixels <= std::numeric_limits<std::uint32_t>::max() && cine.frame_rate > 0;
  const bool counts = frames > 0 && frames <= kMaxFrames && (cine.exposures.empty() || cine.exposures.size() == frames);
  const bool exposures = within(cine.exposure, kMaxShutter) &&
                         std::ranges::all_of(cine.exposures, [](const Duration e) { return within(e, kMaxFrameExposure); });
  const bool times = to_time64(cine.trigger_time).has_value() &&
                     std::ranges::all_of(cine.frame_times, [](const UtcTime t) { return to_time64(t).has_value(); });
  return shape && counts && exposures && times;
}

[[nodiscard]] Layout layout_of(const Cine& cine) {
  const std::size_t frames = cine.frame_times.size();
  const std::size_t time_block = kTagHeaderBytes + (frames * sizeof(std::uint64_t));
  const std::size_t exposure_block = cine.exposures.empty() ? 0 : kTagHeaderBytes + (frames * sizeof(std::uint32_t));
  Layout at;
  at.bitmap = kHeaderBytes;
  at.setup = at.bitmap + kBitmapBytes;
  at.tags = at.setup + kSetupBytes;
  at.offsets = at.tags + time_block + exposure_block;
  at.images = at.offsets + (frames * sizeof(std::uint64_t));
  at.pixels = std::size_t{cine.width} * cine.height * (cine.bit_count / kBitsPerByte);
  at.total = at.images + (frames * (kAnnotationMinimum + at.pixels));
  return at;
}

void put_header(const std::span<std::byte> out, const Cine& cine, const Layout& at) {
  const auto frames = static_cast<std::uint32_t>(cine.frame_times.size());
  put_u16(out, 0, kCineMark);
  put_u16(out, kHeaderSizeAt, kHeaderBytes);
  put_u16(out, kVersionAt, kHeaderVersion);
  put_i32(out, kFirstMovieImageAt, cine.first_image_no);
  put_u32(out, kTotalImageCountAt, frames);
  put_i32(out, kFirstImageNoAt, cine.first_image_no);
  put_u32(out, kImageCountAt, frames);
  put_u32(out, kOffImageHeaderAt, static_cast<std::uint32_t>(at.bitmap));
  put_u32(out, kOffSetupAt, static_cast<std::uint32_t>(at.setup));
  put_u32(out, kOffImageOffsetsAt, static_cast<std::uint32_t>(at.offsets));
  put_u64(out, kTriggerTimeAt, to_time64(cine.trigger_time).value_or(0));
}

void put_bitmap(const std::span<std::byte> out, const Cine& cine, const Layout& at) {
  put_u32(out, at.bitmap, kBitmapBytes);
  put_i32(out, at.bitmap + kBitmapWidthAt, static_cast<std::int32_t>(cine.width));
  put_i32(out, at.bitmap + kBitmapHeightAt, static_cast<std::int32_t>(cine.height));
  put_u16(out, at.bitmap + kBitmapPlanesAt, 1);
  put_u16(out, at.bitmap + kBitmapBitCountAt, cine.bit_count);
  put_u32(out, at.bitmap + kBitmapImageSizeAt, static_cast<std::uint32_t>(at.pixels));
}

void put_setup(const std::span<std::byte> out, const Cine& cine, const Layout& at) {
  put_u16(out, at.setup + kSetupMarkAt, kSetupMark);
  put_u16(out, at.setup + kSetupLengthAt, kSetupBytes);
  put_u32(out, at.setup + kFrameRateAt, cine.frame_rate);
  put_u32(out, at.setup + kSoftwareVersionAt, kSoftwareVersion);
  put_u32(out, at.setup + kRealBppAt, cine.real_bpp);
  put_u32(out, at.setup + kShutterNsAt, static_cast<std::uint32_t>(cine.exposure.count()));
}

// The frame times, first of the tagged blocks; the exposures follow if any.
void put_times(const std::span<std::byte> out, const Cine& cine, const Layout& at) {
  const std::size_t frames = cine.frame_times.size();
  put_u32(out, at.tags, static_cast<std::uint32_t>(kTagHeaderBytes + (frames * sizeof(std::uint64_t))));
  put_u16(out, at.tags + kTagTypeAt, kTimeBlock);
  put_u16(out, at.tags + kTagMoreAt, cine.exposures.empty() ? 0 : 1);
  for (std::size_t i = 0; i < frames; ++i) {
    put_u64(out, at.tags + kTagHeaderBytes + (i * sizeof(std::uint64_t)), to_time64(cine.frame_times[i]).value_or(0));
  }
}

// The frame exposures, each a binary fraction of a second, after the times.
void put_exposures(const std::span<std::byte> out, const Cine& cine, const Layout& at) {
  const std::size_t frames = cine.exposures.size();
  const std::size_t start = at.tags + kTagHeaderBytes + (cine.frame_times.size() * sizeof(std::uint64_t));
  put_u32(out, start, static_cast<std::uint32_t>(kTagHeaderBytes + (frames * sizeof(std::uint32_t))));
  put_u16(out, start + kTagTypeAt, kExposureBlock);
  for (std::size_t i = 0; i < frames; ++i) {
    const auto ns = static_cast<std::uint64_t>(cine.exposures[i].count());
    const std::uint64_t fraction = ((ns << kFractionBits) + (kNsPerSecond / 2)) / kNsPerSecond;
    put_u32(out, start + kTagHeaderBytes + (i * sizeof(std::uint32_t)), static_cast<std::uint32_t>(fraction));
  }
}

// Each frame's image block: an annotation of nothing but its size and the
// image's, then the image, from the images given or all zeros.
void put_images(const std::span<std::byte> out, const Cine& cine, const Layout& at,
                const std::span<const std::byte> images) {
  const std::size_t block = kAnnotationMinimum + at.pixels;
  for (std::size_t i = 0; i < cine.frame_times.size(); ++i) {
    const std::size_t image = at.images + (i * block);
    put_u64(out, at.offsets + (i * sizeof(std::uint64_t)), image);
    put_u32(out, image, kAnnotationMinimum);
    put_u32(out, image + sizeof(std::uint32_t), static_cast<std::uint32_t>(at.pixels));
    if (!images.empty()) {
      store(out, image + kAnnotationMinimum, images.subspan(i * at.pixels, at.pixels));
    }
  }
}

}  // namespace

Result<std::vector<std::byte>> write_cine(const Cine& cine, const std::span<const std::byte> images) {
  if (!writable(cine)) {
    return fail(Error::kInvalidArgument);
  }
  const Layout at = layout_of(cine);
  const bool sized = images.empty() || images.size() == at.pixels * cine.frame_times.size();
  if (at.total > kMaxBytes || !sized) {
    return fail(Error::kInvalidArgument);
  }
  std::vector<std::byte> out(at.total);
  put_header(out, cine, at);
  put_bitmap(out, cine, at);
  put_setup(out, cine, at);
  put_times(out, cine, at);
  if (!cine.exposures.empty()) {
    put_exposures(out, cine, at);
  }
  put_images(out, cine, at, images);
  return out;
}

Result<std::vector<std::byte>> write_cine(const Cine& cine) { return write_cine(cine, {}); }

}  // namespace ics::camera
