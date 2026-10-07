#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"

namespace ics::camera {

// A Phantom camera saves each recording segment as a cine file: a header,
// a bitmap header, the camera's setup, tagged blocks holding each frame's
// time and exposure, a table of where each frame's image starts, and the
// images. ICS reads the layout as the PIMS project's reader (BSD-3-Clause,
// github.com/soft-matter/pims) lays it out; no PIMS code is used. Every
// value is little endian.

// A cine time: seconds since 1970-01-01T00:00:00Z in the high 32 bits and a
// binary fraction of a second in the low 32 bits.
[[nodiscard]] UtcTime from_time64(std::uint64_t time64) noexcept;

// Nothing for a time before 1970 or after the format's last second, in 2106.
// A time survives the round trip through the format to the nanosecond.
[[nodiscard]] std::optional<std::uint64_t> to_time64(UtcTime time) noexcept;

struct Cine {
  // The number of the first frame saved, counted from the trigger frame (0):
  // negative for a segment saved from before its trigger.
  std::int32_t first_image_no = 0;
  // When the camera saw the trigger, by its clock.
  UtcTime trigger_time{};
  // The image size, in pixels, both positive.
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  // Bits each pixel takes in the file, and the bits of it the sensor fills.
  std::uint16_t bit_count = 0;
  std::uint32_t real_bpp = 0;
  // The frame rate set, in frames per second, and the exposure set.
  std::uint32_t frame_rate = 0;
  Duration exposure{};
  // Each saved frame's time, by the camera's clock (tagged block 1002), and
  // its exposure (block 1003), which a cine may leave out.
  std::vector<UtcTime> frame_times;
  std::vector<Duration> exposures;
  // Where each saved frame's image block starts in the file.
  std::vector<std::uint64_t> image_offsets;
};

// Reads a cine's header, bitmap header and setup, and its frame times and
// exposures, and checks that every frame's image lies within the bytes; the
// images themselves are not read. Fails with Error::kEmpty for a cine with
// no frames, and with Error::kMalformed for anything that does not follow
// the layout, including a cine without frame times.
[[nodiscard]] Result<Cine> read_cine(std::span<const std::byte> bytes);

// Writes a cine holding the frame times and exposures given, and the images:
// each frame's pixels in turn, width x height x bit_count / 8 bytes a frame,
// as the camera read them. image_offsets is ignored. Fails with
// Error::kInvalidArgument for images of any other size, a cine read_cine
// would refuse, a time the format cannot hold, an exposure it cannot hold
// (4.29 s for the setup's, under 1 s for a frame's), a side over 65,535
// pixels, a bit count that is not a whole number of bytes, more than 2^20
// frames or a file over 1 GiB.
[[nodiscard]] Result<std::vector<std::byte>> write_cine(const Cine& cine, std::span<const std::byte> images);

// The same, with images of zeros, as the emulated Phantom saves them.
[[nodiscard]] Result<std::vector<std::byte>> write_cine(const Cine& cine);

}  // namespace ics::camera
