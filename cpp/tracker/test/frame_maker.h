// frame_maker.h: synthetic Mono16 frames for the tracker's tests (ICS-038).
#ifndef ICS_TRACKER_TEST_FRAME_MAKER_H
#define ICS_TRACKER_TEST_FRAME_MAKER_H

#include <cmath>
#include <cstddef>
#include <vector>

#include <ics/common/units.hpp>
#include <ics/common/universal_types.h>
#include <ics/tracker/camera.h>

namespace ics::tracker_test
{

inline constexpr int32 byte_span = 256;

// A frame of a flat sky, into which spots are painted.
class Frame_maker
{
public:
  Frame_maker(const int32 width,
              const int32 height,
              const float64 sky)
      : width_(width), height_(height), counts_(static_cast<std::size_t>(width * height), sky)
  {
  }

  // Sets one pixel's count.
  void set(const int32 x,
           const int32 y,
           const float64 count)
  {
    counts_[static_cast<std::size_t>((y * width_) + x)] = count;
  }

  // Adds a Gaussian spot of a sigma and an amplitude, which is negative for a dark spot.
  void spot(const float64 x,
            const float64 y,
            const float64 sigma,
            const float64 amplitude)
  {
    for (int32 row = 0; row < height_; ++row)
    {
      for (int32 column = 0; column < width_; ++column)
      {
        const float64 across = static_cast<float64>(column) - x;
        const float64 down = static_cast<float64>(row) - y;
        const float64 squared = ((across * across) + (down * down)) / (2.0 * sigma * sigma);
        counts_[static_cast<std::size_t>((row * width_) + column)] += amplitude * std::exp(-squared);
      }
    }
  }

  // The frame, its bytes kept in this maker until the next call.
  tracker::Frame frame(const UtcTime time)
  {
    bytes_.clear();
    for (const float64 count : counts_)
    {
      const int32 whole = static_cast<int32>(std::lround(std::max(count, 0.0)));
      bytes_.push_back(static_cast<uint8>(whole % byte_span));
      bytes_.push_back(static_cast<uint8>(whole / byte_span));
    }
    return tracker::Frame{1U, time, width_, height_, tracker::Pixel_format::mono16, bytes_, 0};
  }

private:
  int32 width_;
  int32 height_;
  std::vector<float64> counts_;
  std::vector<uint8> bytes_;
};

}  // namespace ics::tracker_test

#endif  // ICS_TRACKER_TEST_FRAME_MAKER_H
