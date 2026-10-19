// centroid.h: the target in a window of a frame (ICS-038), as
// python/ics_models/tracker/centroid.py finds it. The window's border gives
// the sky background and its noise: the median, and 1.4826 times the median
// absolute deviation. A pixel stands out when its contrast with the
// background, by the target's polarity, passes the larger of k_sigma noise
// sigmas and min_contrast counts. Pixels that stand out and touch, across a
// side or a corner, form a blob. Of the blobs whose area is within bounds,
// the one whose centroid is nearest the prediction is the target; its
// centroid weights each pixel by its contrast. Pixel centres are at whole
// numbers, x to the right and y down.
#ifndef ICS_TRACKER_CENTROID_H
#define ICS_TRACKER_CENTROID_H

// JSF deviation D-6: ics::Result comes from a .hpp header.
#include <vector>

#include <ics/common/error.hpp>
#include <ics/common/universal_types.h>
#include <ics/tracker/camera.h>

namespace ics::tracker
{

// How the target differs from the sky.
enum class Polarity : uint8
{
  dark,
  bright,
  either
};

// A rectangle of a frame: its top-left pixel and its size.
struct Window
{
  int32 x;
  int32 y;
  int32 width;
  int32 height;
};

// A window's size, in pixels.
struct Extent
{
  int32 width;
  int32 height;
};

// A point in a frame, in pixels.
struct Pixel_point
{
  float64 x;
  float64 y;
};

// How a target is told from the sky.
struct Centroid_settings
{
  Polarity polarity;
  float64 k_sigma;
  float64 min_contrast;
  int32 min_area;
  int32 max_area;
};

// A target found: its centroid in frame pixels, its area and peak contrast,
// and the background and noise it stood out from, in counts.
struct Detection
{
  float64 x;
  float64 y;
  int32 area;
  float64 peak;
  float64 background;
  float64 sigma;
};

// The smallest window side: its border needs pixels around an interior.
inline constexpr int32 min_window_side = 3;
// The largest window side a Centroider works on.
inline constexpr int32 max_window_side = 256;

// A window of size centred on the pixel nearest centre, moved inside a
// frame of frame's size. kInvalidArgument for a window that does not fit or
// has a side outside min_window_side to max_window_side; kOutOfRange for a
// centre outside the frame or not finite.
[[nodiscard]] Result<Window> window_around(const Pixel_point centre,
                                           const Extent size,
                                           const Extent frame);

// Finds targets, with room for the largest window allocated once.
class Centroider
{
public:
  explicit Centroider(const Centroid_settings& settings);

  // The target in a window of a frame: kInvalidArgument for a window
  // outside the frame or the sides allowed, kEmpty when no blob is in bounds.
  [[nodiscard]] Result<Detection> find(const Frame& frame,
                                       const Window& window,
                                       const Pixel_point prediction);

  [[nodiscard]] const Centroid_settings& settings() const;

private:
  // The window's background and noise, from its border.
  void measure_background(const Window& window,
                          float64& level,
                          float64& sigma);
  // The median of the first count values of border_, which it reorders. A
  // window's border has 2 (width + height - 2) pixels, an even count, so it
  // is the mean of the middle two.
  [[nodiscard]] float64 median_of(const int32 count);
  // The blob that holds the pixel at start, its pixels marked seen; its
  // contrast-weighted centroid in window pixels, area and peak.
  void blob_at(const int32 start,
               const Window& window,
               Detection& blob);

  Centroid_settings settings_;
  // Each window pixel's contrast, row by row.
  std::vector<float64> contrast_;
  // Whether each pixel stands out, and whether it has joined a blob.
  std::vector<uint8> marks_;
  std::vector<int32> stack_;
  std::vector<float64> border_;
};

}  // namespace ics::tracker

#endif  // ICS_TRACKER_CENTROID_H
