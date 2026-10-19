// golden_test.cpp: ics::tracker against golden/tracker, which the Python reference writes (ICS-038).
// Every outcome, area and window must match, and every other number must be
// within 1e-9 of the larger of it and 1.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <golden_table.h>
#include <ics/common/error.hpp>
#include <ics/common/units.hpp>
#include <ics/common/universal_types.h>
#include <ics/encoder/pointing_model.hpp>
#include <ics/tracker/camera.h>
#include <ics/tracker/centroid.h>
#include <ics/tracker/demand.h>
#include <ics/tracker/estimator.h>
#include <ics/tracker/geometry.h>

namespace
{

using ics::float64;
using ics::int32;
using ics::int64;
using ics::tracker::Angle_state;
using ics::tracker_test::field_of;
using ics::tracker_test::Golden_table;
using ics::tracker_test::integer_of;
using ics::tracker_test::number_of;
using Row = std::vector<std::string>;

constexpr float64 relative = 1.0E-9;
constexpr int32 byte_span = 256;

Golden_table table(const std::string& name)
{
  return ics::tracker_test::read_table(std::string(ICS_GOLDEN_TRACKER_DIR) + "/" + name);
}

void expect_close(const float64 actual,
                  const float64 expected,
                  const std::string& what)
{
  EXPECT_NEAR(actual, expected, relative * std::max(1.0, std::abs(expected))) << what;
}

float64 number(const Golden_table& golden,
               const Row& row,
               const std::string& name)
{
  return number_of(field_of(golden, row, name));
}

int32 whole(const Golden_table& golden,
            const Row& row,
            const std::string& name)
{
  return static_cast<int32>(integer_of(field_of(golden, row, name)));
}

ics::UtcTime time_of(const Golden_table& golden,
                     const Row& row,
                     const std::string& name)
{
  return ics::UtcTime{} + ics::Duration(integer_of(field_of(golden, row, name)));
}

// A golden frame's counts, as Mono16 bytes.
std::vector<ics::uint8> bytes_of(const ics::tracker_test::Golden_image& image)
{
  std::vector<ics::uint8> bytes;
  for (const ics::uint16 count : image.counts)
  {
    const int32 value = static_cast<int32>(count);
    bytes.push_back(static_cast<ics::uint8>(value % byte_span));
    bytes.push_back(static_cast<ics::uint8>(value / byte_span));
  }
  return bytes;
}

ics::tracker::Polarity polarity_of(const std::string& name)
{
  ics::tracker::Polarity polarity = ics::tracker::Polarity::either;
  if (name == "dark")
  {
    polarity = ics::tracker::Polarity::dark;
  }
  else if (name == "bright")
  {
    polarity = ics::tracker::Polarity::bright;
  }
  else
  {
    polarity = ics::tracker::Polarity::either;
  }
  return polarity;
}

void expect_centroid(const Golden_table& golden, const Row& row)
{
  const std::string frame_name = "/frame-0" + field_of(golden, row, "frame") + ".pgm";
  const ics::tracker_test::Golden_image image =
    ics::tracker_test::read_image(std::string(ICS_GOLDEN_TRACKER_DIR) + frame_name);
  const std::vector<ics::uint8> bytes = bytes_of(image);
  const ics::tracker::Frame frame{
    1U, ics::UtcTime{}, image.width, image.height, ics::tracker::Pixel_format::mono16, bytes, 0};
  const ics::tracker::Centroid_settings settings{polarity_of(field_of(golden, row, "polarity")),
                                                 number(golden, row, "k_sigma"),
                                                 number(golden, row, "min_contrast"),
                                                 whole(golden, row, "min_area"),
                                                 whole(golden, row, "max_area")};
  ics::tracker::Centroider centroider(settings);
  const ics::tracker::Window window{
    whole(golden, row, "x"), whole(golden, row, "y"), whole(golden, row, "width"), whole(golden, row, "height")};
  const ics::Result<ics::tracker::Detection> found =
    centroider.find(frame, window, {number(golden, row, "predicted_x"), number(golden, row, "predicted_y")});
  ASSERT_EQ(found.has_value(), whole(golden, row, "ok") == 1) << frame_name;
  if (found.has_value())
  {
    expect_close(found->x, number(golden, row, "found_x"), "x");
    expect_close(found->y, number(golden, row, "found_y"), "y");
    EXPECT_EQ(found->area, whole(golden, row, "area"));
    expect_close(found->peak, number(golden, row, "peak"), "peak");
    expect_close(found->background, number(golden, row, "background"), "background");
    expect_close(found->sigma, number(golden, row, "sigma"), "sigma");
  }
  else
  {
    EXPECT_EQ(found.error(), ics::Error::kEmpty);
  }
}

void expect_geometry(const Golden_table& golden, const Row& row)
{
  const ics::tracker::Camera_geometry camera{number(golden, row, "boresight_x"),
                                             number(golden, row, "boresight_y"),
                                             number(golden, row, "plate_scale"),
                                             number(golden, row, "roll"),
                                             whole(golden, row, "mirrored") == 1};
  const ics::encoder::Direction sight{number(golden, row, "sight_azimuth"), number(golden, row, "sight_elevation")};
  const ics::tracker::Pixel_point pixel{number(golden, row, "x"), number(golden, row, "y")};
  const ics::tracker::Tangent tangent = ics::tracker::tangent_of_pixel(pixel, camera);
  expect_close(tangent.xi, number(golden, row, "xi"), "xi");
  expect_close(tangent.eta, number(golden, row, "eta"), "eta");
  const ics::encoder::Direction sky = ics::tracker::sky_of_pixel(pixel, sight, camera);
  expect_close(sky.azimuth_rad, number(golden, row, "azimuth"), "azimuth");
  expect_close(sky.elevation_rad, number(golden, row, "elevation"), "elevation");
  const ics::Result<ics::tracker::Pixel_point> back = ics::tracker::pixel_of_sky(sky, sight, camera);
  ASSERT_TRUE(back.has_value());
  expect_close(back->x, number(golden, row, "back_x"), "back x");
  expect_close(back->y, number(golden, row, "back_y"), "back y");
}

void expect_state(const Angle_state& state,
                  const Golden_table& golden,
                  const Row& row)
{
  expect_close(state.azimuth_rad, number(golden, row, "azimuth"), "azimuth");
  expect_close(state.elevation_rad, number(golden, row, "elevation"), "elevation");
  expect_close(state.azimuth_rate_rad_s, number(golden, row, "azimuth_rate"), "azimuth rate");
  expect_close(state.elevation_rate_rad_s, number(golden, row, "elevation_rate"), "elevation rate");
}

Angle_state state_of(const Golden_table& golden,
                     const Row& row,
                     const std::string& prefix)
{
  return {time_of(golden, row, prefix + "time_ns"),
          number(golden, row, prefix + "azimuth"),
          number(golden, row, prefix + "elevation"),
          number(golden, row, prefix + "azimuth_rate"),
          number(golden, row, prefix + "elevation_rate")};
}

}  // namespace

// JSF deviation D-5: GoogleTest's TEST and EXPECT macros fix the form of the
// test names and expand to code of GoogleTest's own (AV 50, 51 and 209).
TEST(TrackerGolden, CentroidsEveryWindowAsTheReferenceDoes)
{
  const Golden_table golden = table("centroids.csv");
  ASSERT_FALSE(golden.rows.empty());
  for (const Row& row : golden.rows)
  {
    expect_centroid(golden, row);
  }
}

TEST(TrackerGolden, PlacesEveryWindowAsTheReferenceDoes)
{
  const Golden_table golden = table("windows.csv");
  ASSERT_FALSE(golden.rows.empty());
  for (const Row& row : golden.rows)
  {
    const ics::Result<ics::tracker::Window> window =
      ics::tracker::window_around({number(golden, row, "centre_x"), number(golden, row, "centre_y")},
                                  {whole(golden, row, "width"), whole(golden, row, "height")},
                                  {whole(golden, row, "frame_width"), whole(golden, row, "frame_height")});
    ASSERT_EQ(window.has_value(), whole(golden, row, "ok") == 1);
    if (window.has_value())
    {
      EXPECT_EQ(window->x, whole(golden, row, "x"));
      EXPECT_EQ(window->y, whole(golden, row, "y"));
    }
  }
}

TEST(TrackerGolden, ProjectsEveryPixelAsTheReferenceDoes)
{
  const Golden_table golden = table("geometry.csv");
  ASSERT_FALSE(golden.rows.empty());
  for (const Row& row : golden.rows)
  {
    expect_geometry(golden, row);
  }
}

TEST(TrackerGolden, TracksTheWeaveAsTheReferenceDoes)
{
  const Golden_table golden = table("estimator.csv");
  ASSERT_FALSE(golden.rows.empty());
  const Row& first = golden.rows.front();
  ics::tracker::Angle_estimator track({number(golden, first, "alpha"), number(golden, first, "beta")});
  track.start(Angle_state{time_of(golden, first, "time_ns"),
                          number(golden, first, "azimuth"),
                          number(golden, first, "elevation"),
                          number(golden, first, "azimuth_rate"),
                          number(golden, first, "elevation_rate")});
  const ics::int64 rows = static_cast<ics::int64>(golden.rows.size());
  for (ics::int64 index = 1; index < rows; ++index)
  {
    const Row& row = golden.rows[static_cast<std::size_t>(index)];
    const ics::Result<Angle_state> state =
      track.update(time_of(golden, row, "time_ns"),
                   {number(golden, row, "measured_azimuth"), number(golden, row, "measured_elevation")});
    ASSERT_TRUE(state.has_value());
    expect_state(*state, golden, row);
  }
}

TEST(TrackerGolden, DemandsAsTheReferenceDoes)
{
  const Golden_table golden = table("demands.csv");
  ASSERT_FALSE(golden.rows.empty());
  for (const Row& row : golden.rows)
  {
    const ics::tracker::Axis_rates rates = ics::tracker::demand(
      state_of(golden, row, "target_"), state_of(golden, row, "mount_"), number(golden, row, "gain"));
    expect_close(rates.azimuth_rad_s, number(golden, row, "azimuth_rate"), "azimuth rate");
    expect_close(rates.elevation_rad_s, number(golden, row, "elevation_rate"), "elevation rate");
  }
}
