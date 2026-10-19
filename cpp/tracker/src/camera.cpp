// camera.cpp: a frame's size and pixels (ICS-038).
// JSF deviation D-6: ics::check comes from a .hpp header.
#include <cstddef>

#include <ics/common/check.hpp>
#include <ics/common/universal_types.h>
#include <ics/tracker/camera.h>

namespace
{

using ics::float64;
using ics::int32;
using ics::int64;

// A byte's span of counts, for the high byte of a Mono16 pixel.
constexpr int32 byte_span = 256;

// The bytes a pixel takes.
int64 bytes_per_pixel(const ics::tracker::Pixel_format format)
{
  return (format == ics::tracker::Pixel_format::mono16) ? 2 : 1;
}

}  // namespace

namespace ics::tracker
{

int64 bytes_needed(const int32 width,
                   const int32 height,
                   const Pixel_format format)
{
  return (static_cast<int64>(width) * static_cast<int64>(height)) * bytes_per_pixel(format);
}

float64 pixel_at(const Frame& frame,
                 const int32 x,
                 const int32 y)
{
  float64 count = 0.0;
  const int64 per_pixel = bytes_per_pixel(frame.format);
  const int64 first = ((static_cast<int64>(y) * static_cast<int64>(frame.width)) + static_cast<int64>(x)) * per_pixel;
  const bool inside = (x >= 0) && (x < frame.width) && (y >= 0) && (y < frame.height);
  if (check(inside && ((first + per_pixel) <= static_cast<int64>(frame.bytes.size()))))
  {
    const int32 low = static_cast<int32>(frame.bytes[static_cast<std::size_t>(first)]);
    int32 value = low;
    if (per_pixel == 2)
    {
      const int32 high = static_cast<int32>(frame.bytes[static_cast<std::size_t>(first + 1)]);
      value = low + (high * byte_span);
    }
    count = static_cast<float64>(value);
  }
  return count;
}

}  // namespace ics::tracker
