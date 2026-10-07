#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "bytes.hpp"
#include "ics/camera/cine.hpp"
#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "layout.hpp"

namespace ics::camera {
namespace {

using detail::fits;
using detail::i32;
using detail::kAnnotationMinimum;
using detail::kBitmapBitCountAt;
using detail::kBitmapBytes;
using detail::kBitmapHeightAt;
using detail::kBitmapWidthAt;
using detail::kCineMark;
using detail::kExposureBlock;
using detail::kFirstImageNoAt;
using detail::kFractionBits;
using detail::kFrameRateAt;
using detail::kHeaderBytes;
using detail::kHeaderSizeAt;
using detail::kImageCountAt;
using detail::kImageSizeBytes;
using detail::kNsPerSecond;
using detail::kOffImageHeaderAt;
using detail::kOffImageOffsetsAt;
using detail::kOffSetupAt;
using detail::kRangeBlock;
using detail::kRealBppAt;
using detail::kSetupLengthAt;
using detail::kSetupMark;
using detail::kSetupMarkAt;
using detail::kSetupMinimum;
using detail::kShutterNsAt;
using detail::kTagHeaderBytes;
using detail::kTagMoreAt;
using detail::kTagTypeAt;
using detail::kTimeBlock;
using detail::kTriggerTimeAt;
using detail::u16;
using detail::u32;
using detail::u64;

struct Header {
  std::int32_t first_image_no = 0;
  std::uint32_t image_count = 0;
  std::uint32_t off_image_header = 0;
  std::uint32_t off_setup = 0;
  std::uint32_t off_image_offsets = 0;
  std::uint64_t trigger_time = 0;
};

struct Bitmap {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint16_t bit_count = 0;
};

struct Setup {
  std::uint16_t length = 0;
  std::uint32_t frame_rate = 0;
  std::uint32_t real_bpp = 0;
  std::uint32_t shutter_ns = 0;
};

struct Tagged {
  std::vector<UtcTime> times;
  std::vector<Duration> exposures;
};

struct Block {
  std::uint16_t type = 0;
  bool more = false;
  std::span<const std::byte> body;
};

[[nodiscard]] Result<Header> read_header(const std::span<const std::byte> bytes) {
  if (!fits(bytes.size(), 0, kHeaderBytes) || u16(bytes, 0) != kCineMark || u16(bytes, kHeaderSizeAt) < kHeaderBytes) {
    return fail(Error::kMalformed);
  }
  const Header header{.first_image_no = i32(bytes, kFirstImageNoAt),
                      .image_count = u32(bytes, kImageCountAt),
                      .off_image_header = u32(bytes, kOffImageHeaderAt),
                      .off_setup = u32(bytes, kOffSetupAt),
                      .off_image_offsets = u32(bytes, kOffImageOffsetsAt),
                      .trigger_time = u64(bytes, kTriggerTimeAt)};
  if (header.image_count == 0) {
    return fail(Error::kEmpty);
  }
  return header;
}

[[nodiscard]] Result<Bitmap> read_bitmap(const std::span<const std::byte> bytes, const std::size_t at) {
  if (!fits(bytes.size(), at, kBitmapBytes)) {
    return fail(Error::kMalformed);
  }
  const std::int32_t width = i32(bytes, at + kBitmapWidthAt);
  const std::int32_t height = i32(bytes, at + kBitmapHeightAt);
  const std::uint16_t bit_count = u16(bytes, at + kBitmapBitCountAt);
  if (width <= 0 || height <= 0 || bit_count == 0) {
    return fail(Error::kMalformed);
  }
  return Bitmap{.width = static_cast<std::uint32_t>(width),
                .height = static_cast<std::uint32_t>(height),
                .bit_count = bit_count};
}

[[nodiscard]] Result<Setup> read_setup(const std::span<const std::byte> bytes, const std::size_t at) {
  if (!fits(bytes.size(), at, kSetupMinimum) || u16(bytes, at + kSetupMarkAt) != kSetupMark) {
    return fail(Error::kMalformed);
  }
  const Setup setup{.length = u16(bytes, at + kSetupLengthAt),
                    .frame_rate = u32(bytes, at + kFrameRateAt),
                    .real_bpp = u32(bytes, at + kRealBppAt),
                    .shutter_ns = u32(bytes, at + kShutterNsAt)};
  if (setup.length < kSetupMinimum || !fits(bytes.size(), at, setup.length) || setup.frame_rate == 0) {
    return fail(Error::kMalformed);
  }
  return setup;
}

// The image offset table, which lies within the bytes.
[[nodiscard]] Result<std::vector<std::uint64_t>> read_offsets(const std::span<const std::byte> bytes,
                                                              const std::size_t at, const std::uint32_t count) {
  if (!fits(bytes.size(), at, std::uint64_t{count} * sizeof(std::uint64_t))) {
    return fail(Error::kMalformed);
  }
  std::vector<std::uint64_t> out;
  out.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    out.push_back(u64(bytes, at + (i * sizeof(std::uint64_t))));
  }
  return out;
}

// The tagged block at an offset whose header lies before the end.
[[nodiscard]] Result<Block> block_at(const std::span<const std::byte> bytes, const std::size_t at,
                                     const std::size_t end) {
  const std::uint32_t size = u32(bytes, at);
  if (size < kTagHeaderBytes || !fits(end, at, size)) {
    return fail(Error::kMalformed);
  }
  return Block{.type = u16(bytes, at + kTagTypeAt),
               .more = u16(bytes, at + kTagMoreAt) != 0,
               .body = bytes.subspan(at + kTagHeaderBytes, size - kTagHeaderBytes)};
}

[[nodiscard]] std::vector<UtcTime> times_in(const std::span<const std::byte> body) {
  std::vector<UtcTime> out;
  out.reserve(body.size() / sizeof(std::uint64_t));
  for (std::size_t at = 0; at < body.size(); at += sizeof(std::uint64_t)) {
    out.push_back(from_time64(u64(body, at)));
  }
  return out;
}

// Each exposure is a binary fraction of a second.
[[nodiscard]] std::vector<Duration> exposures_in(const std::span<const std::byte> body) {
  std::vector<Duration> out;
  out.reserve(body.size() / sizeof(std::uint32_t));
  for (std::size_t at = 0; at < body.size(); at += sizeof(std::uint32_t)) {
    const std::uint64_t fraction = u32(body, at);
    const std::uint64_t ns = ((fraction * kNsPerSecond) + (std::uint64_t{1} << (kFractionBits - 1))) >> kFractionBits;
    out.emplace_back(static_cast<Duration::rep>(ns));
  }
  return out;
}

// Keeps a block's frame times or exposures; each must have one per frame.
[[nodiscard]] Status keep(const Block& block, const std::uint32_t count, Tagged& out) {
  const bool times = block.type == kTimeBlock;
  const bool exposures = block.type == kExposureBlock;
  const std::size_t each = times ? sizeof(std::uint64_t) : sizeof(std::uint32_t);
  if ((times || exposures) && block.body.size() != std::uint64_t{count} * each) {
    return fail(Error::kMalformed);
  }
  if (times) {
    out.times = times_in(block.body);
  }
  if (exposures) {
    out.exposures = exposures_in(block.body);
  }
  return {};
}

// The tagged blocks from start to end, which lie within the bytes.
[[nodiscard]] Result<Tagged> read_tagged(const std::span<const std::byte> bytes, const std::size_t start,
                                         const std::size_t end, const std::uint32_t count) {
  Tagged out;
  std::size_t at = start;
  bool more = true;
  while (more && fits(end, at, kTagHeaderBytes)) {
    const Result<Block> block = block_at(bytes, at, end);
    const Status kept = block ? keep(*block, count, out) : fail(block.error());
    if (!kept) {
      return fail(kept.error());
    }
    at += kTagHeaderBytes + block->body.size();
    more = block->more && block->type != kRangeBlock;
  }
  if (out.times.size() != count) {
    return fail(Error::kMalformed);
  }
  return out;
}

// Whether each frame's annotation and image lie within the bytes.
[[nodiscard]] Status check_images(const std::span<const std::byte> bytes, const std::span<const std::uint64_t> offsets) {
  for (const std::uint64_t at : offsets) {
    const bool headed = fits(bytes.size(), at, kAnnotationMinimum);
    const std::uint32_t annotation = headed ? u32(bytes, at) : 0;
    const bool annotated = annotation >= kAnnotationMinimum && fits(bytes.size(), at, annotation);
    const std::uint32_t image = annotated ? u32(bytes, at + annotation - kImageSizeBytes) : 0;
    if (image == 0 || !fits(bytes.size(), at + annotation, image)) {
      return fail(Error::kMalformed);
    }
  }
  return {};
}

}  // namespace

UtcTime from_time64(const std::uint64_t time64) noexcept {
  const std::uint64_t seconds = time64 >> kFractionBits;
  const std::uint64_t fraction = time64 & std::numeric_limits<std::uint32_t>::max();
  const std::uint64_t ns = ((fraction * kNsPerSecond) + (std::uint64_t{1} << (kFractionBits - 1))) >> kFractionBits;
  return utc_from_ns(static_cast<std::int64_t>((seconds * kNsPerSecond) + ns));
}

std::optional<std::uint64_t> to_time64(const UtcTime time) noexcept {
  const std::int64_t ns = to_utc_ns(time);
  const std::uint64_t seconds = static_cast<std::uint64_t>(ns) / kNsPerSecond;
  if (ns < 0 || seconds > std::numeric_limits<std::uint32_t>::max()) {
    return std::nullopt;
  }
  const std::uint64_t part = static_cast<std::uint64_t>(ns) % kNsPerSecond;
  const std::uint64_t fraction = ((part << kFractionBits) + (kNsPerSecond / 2)) / kNsPerSecond;
  return (seconds << kFractionBits) | fraction;
}

Result<Cine> read_cine(const std::span<const std::byte> bytes) {
  const Result<Header> header = read_header(bytes);
  const Result<Bitmap> bitmap = header ? read_bitmap(bytes, header->off_image_header) : fail(header.error());
  const Result<Setup> setup = bitmap ? read_setup(bytes, header->off_setup) : fail(bitmap.error());
  if (!setup) {
    return fail(setup.error());
  }
  const Result<std::vector<std::uint64_t>> offsets =
      read_offsets(bytes, header->off_image_offsets, header->image_count);
  const std::size_t tags = std::size_t{header->off_setup} + setup->length;
  Result<Tagged> tagged = offsets && tags <= header->off_image_offsets
                              ? read_tagged(bytes, tags, header->off_image_offsets, header->image_count)
                              : fail(Error::kMalformed);
  const Status images = tagged ? check_images(bytes, *offsets) : fail(tagged.error());
  if (!images) {
    return fail(images.error());
  }
  return Cine{.first_image_no = header->first_image_no,
              .trigger_time = from_time64(header->trigger_time),
              .width = bitmap->width,
              .height = bitmap->height,
              .bit_count = bitmap->bit_count,
              .real_bpp = setup->real_bpp,
              .frame_rate = setup->frame_rate,
              .exposure = Duration(setup->shutter_ns),
              .frame_times = std::move(tagged->times),
              .exposures = std::move(tagged->exposures),
              .image_offsets = *offsets};
}

}  // namespace ics::camera
