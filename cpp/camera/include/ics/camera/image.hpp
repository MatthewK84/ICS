#pragma once

#include <cstddef>
#include <span>

#include "ics/camera/cine.hpp"
#include "ics/camera/strobe.hpp"
#include "ics/common/error.hpp"

namespace ics::camera {

// The mean pixel value in a rectangle of a frame's image (ICS-029), from the
// bytes read_cine read the cine from. Pixels are unpacked 8- or 16-bit
// little-endian words, row by row as the image stores its rows. Fails with
// Error::kOutOfRange for a frame past the cine's or a rectangle not within
// the image, Error::kInvalidArgument for an empty rectangle, and
// Error::kMalformed for pixels of any other size or an image block that does
// not hold the image.
[[nodiscard]] Result<double> roi_mean(std::span<const std::byte> bytes, const Cine& cine, std::size_t frame,
                                      const Roi& roi);

}  // namespace ics::camera
