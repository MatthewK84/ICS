// centroid.cpp: the target in a window of a frame (ICS-038).
// JSF deviation D-6: ics::Result and ics::check come from .hpp headers.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

#include <ics/common/error.hpp>
#include <ics/common/universal_types.h>
#include <ics/tracker/camera.h>
#include <ics/tracker/centroid.h>

namespace
{

using ics::Error;
using ics::fail;
using ics::float64;
using ics::int32;
using ics::uint8;
using ics::tracker::max_window_side;
using ics::tracker::min_window_side;
using ics::tracker::Polarity;
using ics::tracker::Window;

// The median absolute deviation of Gaussian noise, in sigmas.
constexpr float64 mad_to_sigma = 1.4826;
// Room for every pixel of the largest window, and for its border.
constexpr int32 max_pixels = max_window_side * max_window_side;
constexpr int32 max_border = 4 * max_window_side;
// A pixel's marks.
constexpr uint8 standing_out = 1U;
constexpr uint8 joined = 2U;
// The eight neighbours of a pixel, as (row, column) steps, in the reference's order.
constexpr int32 neighbours = 8;
constexpr std::array<int32, neighbours> neighbour_rows{-1, -1, -1, 0, 0, 1, 1, 1};
constexpr std::array<int32, neighbours> neighbour_columns{-1, 0, 1, -1, 1, -1, 0, 1};

// Whether a window has sides the Centroider takes.
bool sized(const int32 width, const int32 height)
{
  const bool wide = (width >= min_window_side) && (width <= max_window_side);
  return wide && (height >= min_window_side) && (height <= max_window_side);
}

// A pixel's contrast with the background, in the target's polarity.
float64 contrast_of(const float64 count,
                    const float64 background,
                    const Polarity polarity)
{
  float64 contrast = 0.0;
  if (polarity == Polarity::dark)
  {
    contrast = background - count;
  }
  else if (polarity == Polarity::bright)
  {
    contrast = count - background;
  }
  else
  {
    contrast = std::abs(count - background);
  }
  return contrast;
}

}  // namespace

namespace ics::tracker
{

Result<Window> window_around(const Pixel_point centre,
                             const Extent size,
                             const Extent frame)
{
  Result<Window> window = fail(Error::kInvalidArgument);
  if (sized(size.width, size.height) && (size.width <= frame.width) && (size.height <= frame.height))
  {
    const bool finite = std::isfinite(centre.x) && std::isfinite(centre.y);
    const bool across = (centre.x >= 0.0) && (centre.x < static_cast<float64>(frame.width));
    const bool down = (centre.y >= 0.0) && (centre.y < static_cast<float64>(frame.height));
    window = fail(Error::kOutOfRange);
    if (finite && across && down)
    {
      const int32 column = static_cast<int32>(std::nearbyint(centre.x));
      const int32 row = static_cast<int32>(std::nearbyint(centre.y));
      const int32 left = std::min(std::max(column - (size.width / 2), 0), frame.width - size.width);
      const int32 top = std::min(std::max(row - (size.height / 2), 0), frame.height - size.height);
      window = Window{left, top, size.width, size.height};
    }
  }
  return window;
}

Centroider::Centroider(const Centroid_settings& settings)
    : settings_(settings),
      contrast_(static_cast<std::size_t>(max_pixels), 0.0),
      marks_(static_cast<std::size_t>(max_pixels), 0U),
      stack_(static_cast<std::size_t>(max_pixels), 0),
      border_(static_cast<std::size_t>(max_border), 0.0)
{
}

const Centroid_settings& Centroider::settings() const
{
  return settings_;
}

float64 Centroider::median_of(const int32 count)
{
  const std::vector<float64>::iterator first = border_.begin();
  const std::vector<float64>::iterator upper = first + (count / 2);
  std::nth_element(first, upper, first + count);
  const float64 lower = *std::max_element(first, upper);
  return (lower + *upper) / 2.0;
}

void Centroider::measure_background(const Window& window,
                                    float64& level,
                                    float64& sigma)
{
  int32 count = 0;
  for (int32 row = 0; row < window.height; ++row)
  {
    const bool edge_row = (row == 0) || (row == (window.height - 1));
    for (int32 column = 0; column < window.width; ++column)
    {
      const bool edge = edge_row || (column == 0) || (column == (window.width - 1));
      if (edge)
      {
        border_[static_cast<std::size_t>(count)] = contrast_[static_cast<std::size_t>((row * window.width) + column)];
        count += 1;
      }
    }
  }
  level = median_of(count);
  for (int32 index = 0; index < count; ++index)
  {
    border_[static_cast<std::size_t>(index)] = std::abs(border_[static_cast<std::size_t>(index)] - level);
  }
  sigma = mad_to_sigma * median_of(count);
}

void Centroider::blob_at(const int32 start,
                         const Window& window,
                         Detection& blob)
{
  float64 total = 0.0;
  float64 weighted_x = 0.0;
  float64 weighted_y = 0.0;
  int32 depth = 1;
  blob.area = 0;
  blob.peak = -std::numeric_limits<float64>::infinity();
  stack_[0] = start;
  marks_[static_cast<std::size_t>(start)] = joined;
  while (depth > 0)
  {
    depth -= 1;
    const int32 here = stack_[static_cast<std::size_t>(depth)];
    const int32 row = here / window.width;
    const int32 column = here % window.width;
    const float64 weight = contrast_[static_cast<std::size_t>(here)];
    total += weight;
    weighted_x += weight * static_cast<float64>(column);
    weighted_y += weight * static_cast<float64>(row);
    blob.peak = std::max(blob.peak, weight);
    blob.area += 1;
    for (int32 step = 0; step < neighbours; ++step)
    {
      const int32 near_row = row + neighbour_rows[static_cast<std::size_t>(step)];
      const int32 near_column = column + neighbour_columns[static_cast<std::size_t>(step)];
      const bool inside = (near_row >= 0) && (near_row < window.height) && (near_column >= 0) &&
                          (near_column < window.width);
      const int32 near = inside ? ((near_row * window.width) + near_column) : start;
      if (inside && (marks_[static_cast<std::size_t>(near)] == standing_out))
      {
        marks_[static_cast<std::size_t>(near)] = joined;
        stack_[static_cast<std::size_t>(depth)] = near;
        depth += 1;
      }
    }
  }
  blob.x = weighted_x / total;
  blob.y = weighted_y / total;
}

Result<Detection> Centroider::find(const Frame& frame,
                                   const Window& window,
                                   const Pixel_point prediction)
{
  Result<Detection> found = fail(Error::kInvalidArgument);
  const bool placed = (window.x >= 0) && (window.y >= 0) && ((window.x + window.width) <= frame.width) &&
                      ((window.y + window.height) <= frame.height);
  if (sized(window.width, window.height) && placed)
  {
    const int32 pixels = window.width * window.height;
    for (int32 index = 0; index < pixels; ++index)
    {
      const int32 x = window.x + (index % window.width);
      const int32 y = window.y + (index / window.width);
      contrast_[static_cast<std::size_t>(index)] = pixel_at(frame, x, y);
    }
    float64 background = 0.0;
    float64 sigma = 0.0;
    measure_background(window, background, sigma);
    const float64 threshold = std::max(settings_.k_sigma * sigma, settings_.min_contrast);
    for (int32 index = 0; index < pixels; ++index)
    {
      const float64 contrast = contrast_of(contrast_[static_cast<std::size_t>(index)], background, settings_.polarity);
      contrast_[static_cast<std::size_t>(index)] = contrast;
      marks_[static_cast<std::size_t>(index)] = (contrast > threshold) ? standing_out : 0U;
    }
    found = fail(Error::kEmpty);
    float64 best_distance = std::numeric_limits<float64>::infinity();
    for (int32 index = 0; index < pixels; ++index)
    {
      if (marks_[static_cast<std::size_t>(index)] == standing_out)
      {
        Detection blob{0.0, 0.0, 0, 0.0, background, sigma};
        blob_at(index, window, blob);
        blob.x += static_cast<float64>(window.x);
        blob.y += static_cast<float64>(window.y);
        const float64 distance = std::hypot(blob.x - prediction.x, blob.y - prediction.y);
        const bool bounded = (blob.area >= settings_.min_area) && (blob.area <= settings_.max_area);
        if (bounded && (distance < best_distance))
        {
          found = blob;
          best_distance = distance;
        }
      }
    }
  }
  return found;
}

}  // namespace ics::tracker
