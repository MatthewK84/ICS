#pragma once

#include <cstddef>
#include <span>

#include "ics/camera/strobe.hpp"
#include "ics/common/error.hpp"
#include "ics/common/units.hpp"

namespace ics::strobe {

// A frame as the strobe analyzer sees it: when the camera stamped its
// exposure's start, how long the exposure lasted, and the mean brightness of
// the strobe's rectangle, in counts.
struct StrobeFrame {
  UtcTime stamp{};
  Duration exposure{};
  double brightness = 0.0;
};

// The camera offset that best explains a set of frames, and how well.
struct OffsetFit {
  // The camera's stamp minus the true start of its exposure, and its
  // one-sigma uncertainty.
  Duration offset{};
  Duration sigma{};
  // Counts for each microsecond of strobe light in an exposure, and counts
  // with none.
  double gain_per_us = 0.0;
  double background = 0.0;
  // The residuals' RMS, in counts.
  double rms = 0.0;
  std::size_t frames = 0;
  // Frames partly lit at the offset: the ones whose brightness moves with
  // it, and so pin it.
  std::size_t edge_frames = 0;
};

// Fits brightness = gain x strobe light + background, each frame's strobe
// light taken from its stamp less the offset, to offsets within max_offset of
// 0: on a 250 ns grid, then to the nanosecond around the grid's best. The
// sigma is the residuals' RMS over the information the frames carry about
// the offset once gain and background are fitted too. Fails with
// Error::kEmpty for fewer than 4 frames, and with Error::kUnconstrained when
// the frames carry no information about the best offset (no partly lit
// frame, or every lit frame holding the same light, as with no sweep, so that
// the offset trades off against the gain), it lies at the search's edge, or
// it explains the frames
// poorly: a full pulse's brightness under ten times the residuals' RMS, as
// when the true offset lies beyond the search.
[[nodiscard]] Result<OffsetFit> fit_offset(std::span<const StrobeFrame> frames, const camera::StrobeSchedule& schedule,
                                           Duration max_offset);

}  // namespace ics::strobe
