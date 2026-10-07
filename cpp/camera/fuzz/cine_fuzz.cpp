// Fuzzes the cine reader (ICS-027) with any bytes, as a camera link or a
// damaged file could deliver them. It may not overflow or crash, and a cine
// it reads must keep its promises:
// - one time and one image offset per frame, and an exposure per frame or
//   none; a size and a frame rate that are not zero;
// - frame metadata and a verification for every frame;
// - a rectangle's mean brightness, read from every frame's image without
//   reading past the bytes, as a number from 0 to 65,535;
// - a cine the writer can write reads back as the same cine. The writer
//   fills every image, so a few bytes can ask it for a gibibyte: only cines
//   whose images fit in 16 MiB make the round trip.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <vector>

#include "ics/camera/cine.hpp"
#include "ics/camera/frame_meta.hpp"
#include "ics/camera/image.hpp"
#include "ics/camera/offload.hpp"
#include "ics/common/error.hpp"
#include "ics/v1/camera_frame_meta.pb.h"

namespace {

using ics::camera::Cine;

constexpr std::uint64_t kRoundTripBytes = std::uint64_t{1} << 24U;
constexpr std::uint16_t kBitsPerByte = 8;

void require(const bool condition) {
  if (!condition) {
    std::abort();
  }
}

void require_sound(const Cine& cine) {
  const std::size_t frames = cine.frame_times.size();
  require(frames > 0 && cine.image_offsets.size() == frames);
  require(cine.exposures.empty() || cine.exposures.size() == frames);
  require(cine.width > 0 && cine.height > 0 && cine.bit_count > 0 && cine.frame_rate > 0);
  const std::vector<ics::v1::CameraFrameMeta> meta = ics::camera::frame_meta(cine, {}, {});
  require(meta.size() == frames);
  const ics::camera::SegmentCheck check = ics::camera::verify_segment(cine, {});
  require(check.frames == frames && check.spacing_error.count() >= 0 && check.trigger_error.count() >= 0);
}

// Up to the first 8 x 8 pixels of each frame.
void require_readable_images(const std::span<const std::byte> input, const Cine& cine) {
  constexpr std::uint32_t kSide = 8;
  constexpr double kMaxPixel = 65'535.0;
  const ics::camera::Roi corner{.x = 0, .y = 0, .width = std::min(cine.width, kSide), .height = std::min(cine.height, kSide)};
  for (std::size_t frame = 0; frame < cine.image_offsets.size(); ++frame) {
    const ics::Result<double> mean = ics::camera::roi_mean(input, cine, frame, corner);
    require(!mean || (std::isfinite(*mean) && *mean >= 0.0 && *mean <= kMaxPixel));
  }
}

bool small(const Cine& cine) {
  const std::uint64_t pixels = std::uint64_t{cine.width} * cine.height;
  if (pixels > kRoundTripBytes) {
    return false;
  }
  const std::uint64_t frame = pixels * (cine.bit_count / kBitsPerByte);
  return frame <= kRoundTripBytes && frame * cine.frame_times.size() <= kRoundTripBytes;
}

void require_round_trip(const Cine& cine) {
  const ics::Result<std::vector<std::byte>> written = ics::camera::write_cine(cine);
  if (!written) {
    return;
  }
  const ics::Result<Cine> again = ics::camera::read_cine(*written);
  require(again.has_value());
  require(again->frame_times == cine.frame_times && again->exposures == cine.exposures);
  require(again->first_image_no == cine.first_image_no && again->trigger_time == cine.trigger_time);
  require(again->width == cine.width && again->height == cine.height && again->bit_count == cine.bit_count);
  require(again->real_bpp == cine.real_bpp && again->frame_rate == cine.frame_rate && again->exposure == cine.exposure);
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  const std::span<const std::byte> input = std::as_bytes(std::span(data, size));
  const ics::Result<Cine> cine = ics::camera::read_cine(input);
  if (cine) {
    require_sound(*cine);
    require_readable_images(input, *cine);
  }
  if (cine && small(*cine)) {
    require_round_trip(*cine);
  }
  return 0;
}
