#include "ics/camera/image.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

#include "bytes.hpp"
#include "ics/camera/cine.hpp"
#include "ics/camera/strobe.hpp"
#include "ics/common/check.hpp"
#include "ics/common/error.hpp"
#include "layout.hpp"

namespace ics::camera {
namespace {

using detail::fits;
using detail::kAnnotationMinimum;
using detail::kImageSizeBytes;
using detail::u16;
using detail::u32;

constexpr std::uint16_t kByteBits = 8;
constexpr std::uint16_t kWordBits = 16;

// Where the frame's pixels start in bytes, if its image block holds them.
[[nodiscard]] Result<std::size_t> pixels_at(const std::span<const std::byte> bytes, const Cine& cine,
                                            const std::size_t frame) {
  const std::uint64_t at = cine.image_offsets[frame];
  const std::uint32_t annotation = fits(bytes.size(), at, kAnnotationMinimum) ? u32(bytes, at) : 0;
  const bool annotated = annotation >= kAnnotationMinimum && fits(bytes.size(), at, annotation);
  const std::uint32_t image = annotated ? u32(bytes, at + annotation - kImageSizeBytes) : 0;
  const std::uint64_t needed = std::uint64_t{cine.width} * cine.height * (cine.bit_count / kByteBits);
  if (!annotated || image < needed || !fits(bytes.size(), at + annotation, needed)) {
    return fail(Error::kMalformed);
  }
  return static_cast<std::size_t>(at + annotation);
}

[[nodiscard]] double roi_sum(const std::span<const std::byte> bytes, const Cine& cine, const std::size_t start,
                             const Roi& roi) {
  const bool words = cine.bit_count == kWordBits;
  const std::size_t depth = words ? 2 : 1;
  // pixels_at found the whole image within the bytes, and the rectangle lies
  // within the image.
  const std::size_t last = start + ((((std::size_t{roi.y} + roi.height - 1) * cine.width) + roi.x + roi.width) * depth);
  static_cast<void>(check(last <= bytes.size()));
  double sum = 0.0;
  for (std::uint32_t row = roi.y; row < roi.y + roi.height; ++row) {
    for (std::uint32_t column = roi.x; column < roi.x + roi.width; ++column) {
      const std::size_t at = start + (((std::size_t{row} * cine.width) + column) * depth);
      sum += words ? u16(bytes, at) : std::to_integer<std::uint8_t>(bytes[at]);
    }
  }
  return sum;
}

}  // namespace

Result<double> roi_mean(const std::span<const std::byte> bytes, const Cine& cine, const std::size_t frame,
                        const Roi& roi) {
  if (roi.width == 0 || roi.height == 0) {
    return fail(Error::kInvalidArgument);
  }
  const bool inside = std::uint64_t{roi.x} + roi.width <= cine.width && std::uint64_t{roi.y} + roi.height <= cine.height;
  if (frame >= cine.image_offsets.size() || !inside) {
    return fail(Error::kOutOfRange);
  }
  if (cine.bit_count != kByteBits && cine.bit_count != kWordBits) {
    return fail(Error::kMalformed);
  }
  const Result<std::size_t> start = pixels_at(bytes, cine, frame);
  if (!start) {
    return fail(start.error());
  }
  return roi_sum(bytes, cine, *start, roi) / (static_cast<double>(roi.width) * roi.height);
}

}  // namespace ics::camera
