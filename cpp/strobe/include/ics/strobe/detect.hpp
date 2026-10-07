#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "ics/camera/cine.hpp"
#include "ics/camera/strobe.hpp"
#include "ics/common/error.hpp"
#include "ics/strobe/fit.hpp"

namespace ics::strobe {

// One recorded segment around a PPS, as the strobe analyzer reads it.
struct StrobeSegment {
  // The UTC second, counted from 1970, nearest the segment's middle frame:
  // the one whose pulse it holds.
  std::int64_t second = 0;
  std::vector<StrobeFrame> frames;
  // The frames the strobe lit, and whether they follow one another.
  std::size_t lit = 0;
  bool contiguous = true;
};

// Reads the strobe rectangle's mean brightness in each frame of a segment and
// finds the frames the strobe lit: brighter than the segment's median by six
// robust sigmas (1.4826 median absolute deviations), and by at least one
// count. Fails with Error::kEmpty for a cine of fewer than 3 frames, and as
// camera::roi_mean does.
[[nodiscard]] Result<StrobeSegment> read_segment(std::span<const std::byte> bytes, const camera::Cine& cine,
                                                 const camera::Roi& roi);

// The lit frames of brightnesses, by the rule above, as flags.
[[nodiscard]] std::vector<bool> lit_frames(std::span<const double> brightness);

}  // namespace ics::strobe
