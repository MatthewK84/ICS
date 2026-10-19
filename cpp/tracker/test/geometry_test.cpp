// geometry_test.cpp: pixels and directions on the sky (ICS-038).
#include <array>
#include <numbers>

#include <gtest/gtest.h>

#include <ics/common/error.hpp>
#include <ics/common/universal_types.h>
#include <ics/encoder/pointing_model.hpp>
#include <ics/tracker/centroid.h>
#include <ics/tracker/geometry.h>

namespace
{

using ics::float64;
using ics::encoder::Direction;
using ics::tracker::Camera_geometry;

constexpr float64 boresight_x = 32.0;
constexpr float64 boresight_y = 24.0;
constexpr float64 plate_scale = 1.0E-5;
constexpr float64 ten_pixels = 10.0;
constexpr float64 offset = 1.0E-4;
constexpr float64 tight = 1.0E-15;
constexpr float64 pixel_tolerance = 1.0E-6;
constexpr float64 relative = 1.0E-6;
constexpr float64 full_turn = 2.0 * std::numbers::pi;
constexpr float64 quarter_turn = std::numbers::pi / 2.0;
constexpr float64 tiny_negative = -1.0E-17;
constexpr float64 past_north = 0.01;
constexpr float64 low_elevation = 0.2;
constexpr Direction sight{1.0, 0.5};
constexpr Camera_geometry camera{boresight_x, boresight_y, plate_scale, 0.0, false};

}  // namespace

// JSF deviation D-5: GoogleTest's TEST and EXPECT macros fix the form of the
// test names and expand to code of GoogleTest's own (AV 50, 51 and 209).
TEST(TrackerGeometry, LooksAlongTheLineOfSightAtTheBoresight)
{
  const Direction sky = ics::tracker::sky_of_pixel({boresight_x, boresight_y}, sight, camera);
  EXPECT_NEAR(sky.azimuth_rad, sight.azimuth_rad, tight);
  EXPECT_NEAR(sky.elevation_rad, sight.elevation_rad, tight);
}

TEST(TrackerGeometry, TurnsRightIntoAzimuthAndUpIntoElevation)
{
  const ics::tracker::Tangent right = ics::tracker::tangent_of_pixel({boresight_x + ten_pixels, boresight_y}, camera);
  EXPECT_NEAR(right.xi, offset, tight);
  EXPECT_NEAR(right.eta, 0.0, tight);
  const ics::tracker::Tangent up = ics::tracker::tangent_of_pixel({boresight_x, boresight_y - ten_pixels}, camera);
  EXPECT_NEAR(up.eta, offset, tight);
  const Direction above = ics::tracker::sky_of_pixel({boresight_x, boresight_y - ten_pixels}, sight, camera);
  EXPECT_NEAR(above.elevation_rad - sight.elevation_rad, offset, offset * relative);
}

TEST(TrackerGeometry, RollsAndMirrorsTheImage)
{
  const Camera_geometry rolled{boresight_x, boresight_y, plate_scale, quarter_turn, false};
  const ics::tracker::Tangent turned = ics::tracker::tangent_of_pixel({boresight_x + ten_pixels, boresight_y}, rolled);
  EXPECT_NEAR(turned.xi, 0.0, tight);
  EXPECT_NEAR(turned.eta, offset, tight);
  const Camera_geometry mirrored{boresight_x, boresight_y, plate_scale, 0.0, true};
  EXPECT_NEAR(ics::tracker::tangent_of_pixel({boresight_x + ten_pixels, boresight_y}, mirrored).xi, -offset, tight);
}

TEST(TrackerGeometry, RoundTripsPixelsThroughTheSky)
{
  const auto cameras = std::to_array<Camera_geometry>({camera, {boresight_x, boresight_y, plate_scale, -2.0, true}});
  const auto pixels =
    std::to_array<ics::tracker::Pixel_point>({{0.0, 0.0}, {boresight_x, 1.0}, {ten_pixels, boresight_y}});
  for (const Camera_geometry& each : cameras)
  {
    for (const ics::tracker::Pixel_point pixel : pixels)
    {
      const ics::Result<ics::tracker::Pixel_point> back =
        ics::tracker::pixel_of_sky(ics::tracker::sky_of_pixel(pixel, sight, each), sight, each);
      ASSERT_TRUE(back.has_value());
      EXPECT_NEAR(back->x, pixel.x, pixel_tolerance);
      EXPECT_NEAR(back->y, pixel.y, pixel_tolerance);
    }
  }
}

TEST(TrackerGeometry, WrapsAzimuthIntoOneTurn)
{
  EXPECT_EQ(ics::tracker::wrap_azimuth(tiny_negative), 0.0);
  EXPECT_NEAR(ics::tracker::wrap_azimuth(full_turn + 0.5), 0.5, tight);
  EXPECT_NEAR(ics::tracker::wrap_azimuth(-0.5), full_turn - 0.5, tight);
  const Direction west = ics::tracker::sky_of_tangent({-past_north, 0.0}, {0.0, low_elevation});
  EXPECT_GT(west.azimuth_rad, full_turn - (2.0 * past_north));
}

TEST(TrackerGeometry, HasNoPixelBehindTheCamera)
{
  const Direction behind{sight.azimuth_rad + std::numbers::pi, 0.0};
  EXPECT_EQ(ics::tracker::tangent_of_sky(behind, {sight.azimuth_rad, 0.0}).error(), ics::Error::kOutOfRange);
  EXPECT_EQ(ics::tracker::pixel_of_sky(behind, {sight.azimuth_rad, 0.0}, camera).error(), ics::Error::kOutOfRange);
}
