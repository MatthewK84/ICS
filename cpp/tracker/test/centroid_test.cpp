// centroid_test.cpp: finding the target in a window (ICS-038).
#include <array>
#include <limits>

#include <gtest/gtest.h>

#include <frame_maker.h>
#include <ics/common/error.hpp>
#include <ics/common/units.hpp>
#include <ics/common/universal_types.h>
#include <ics/testing/no_allocation_scope.hpp>
#include <ics/tracker/camera.h>
#include <ics/tracker/centroid.h>

namespace
{

using ics::float64;
using ics::int32;
using ics::tracker::Centroid_settings;
using ics::tracker::Polarity;
using ics::tracker::Window;
using ics::tracker_test::Frame_maker;

constexpr int32 width = 20;
constexpr int32 height = 10;
constexpr float64 sky = 100.0;
constexpr float64 bright = 150.0;
constexpr float64 faint = 110.0;
constexpr float64 dim = 130.0;
constexpr float64 dark = 50.0;
constexpr int32 spot_x = 7;
constexpr int32 spot_y = 4;
constexpr float64 weighted_x = 7.25;
constexpr int32 far_x = 15;
constexpr int32 far_y = 7;
constexpr float64 contrast = 50.0;
constexpr float64 k_sigma = 5.0;
constexpr float64 min_contrast = 10.0;
constexpr int32 max_area = 50;
constexpr int32 big_side = 300;
constexpr int32 big_frame = 400;
constexpr float64 near_edge = 19.9;
constexpr ics::tracker::Extent window_size{5, 3};
constexpr ics::tracker::Extent frame_size{width, height};
constexpr ics::tracker::Pixel_point origin{0.0, 0.0};

Centroid_settings settings(const Polarity polarity)
{
  return {polarity, k_sigma, min_contrast, 1, max_area};
}

constexpr Window whole{0, 0, width, height};

}  // namespace

// JSF deviation D-5: GoogleTest's TEST and EXPECT macros fix the form of the
// test names and expand to code of GoogleTest's own (AV 50, 51 and 209).
TEST(TrackerCentroid, FindsABrightPixelWhereItIs)
{
  Frame_maker maker(width, height, sky);
  maker.set(spot_x, spot_y, bright);
  ics::tracker::Centroider centroider(settings(Polarity::bright));
  const ics::tracker::Frame frame = maker.frame(ics::UtcTime{});
  const ics::testing::NoAllocationScope no_allocation;
  const ics::Result<ics::tracker::Detection> found = centroider.find(frame, whole, origin);
  ASSERT_TRUE(found.has_value());
  EXPECT_DOUBLE_EQ(found->x, static_cast<float64>(spot_x));
  EXPECT_DOUBLE_EQ(found->y, static_cast<float64>(spot_y));
  EXPECT_EQ(found->area, 1);
  EXPECT_DOUBLE_EQ(found->peak, contrast);
  EXPECT_DOUBLE_EQ(found->background, sky);
  EXPECT_DOUBLE_EQ(found->sigma, 0.0);
}

TEST(TrackerCentroid, WeighsEachPixelByItsContrast)
{
  Frame_maker maker(width, height, sky);
  maker.set(spot_x, spot_y, dim);
  maker.set(spot_x + 1, spot_y, faint);
  ics::tracker::Centroider centroider({Polarity::bright, k_sigma, min_contrast / 2.0, 1, max_area});
  const ics::Result<ics::tracker::Detection> found = centroider.find(maker.frame(ics::UtcTime{}), whole, origin);
  ASSERT_TRUE(found.has_value());
  EXPECT_DOUBLE_EQ(found->x, weighted_x);
  EXPECT_EQ(found->area, 2);
}

TEST(TrackerCentroid, TakesTheBlobNearestThePrediction)
{
  Frame_maker maker(width, height, sky);
  maker.set(spot_x, spot_y, bright);
  maker.set(far_x, far_y, bright);
  maker.set(0, 0, bright);
  ics::tracker::Centroider centroider(settings(Polarity::either));
  const ics::tracker::Frame frame = maker.frame(ics::UtcTime{});
  EXPECT_DOUBLE_EQ(centroider.find(frame, whole, {static_cast<float64>(far_x), 0.0})->x, static_cast<float64>(far_x));
  EXPECT_DOUBLE_EQ(centroider.find(frame, whole, {static_cast<float64>(spot_x), 0.0})->x, static_cast<float64>(spot_x));
}

TEST(TrackerCentroid, ReadsEachPolarity)
{
  Frame_maker maker(width, height, sky);
  maker.set(spot_x, spot_y, dark);
  const ics::tracker::Frame frame = maker.frame(ics::UtcTime{});
  ics::tracker::Centroider dark_sky(settings(Polarity::dark));
  EXPECT_TRUE(dark_sky.find(frame, whole, origin).has_value());
  ics::tracker::Centroider either(settings(Polarity::either));
  EXPECT_TRUE(either.find(frame, whole, origin).has_value());
  ics::tracker::Centroider night(settings(Polarity::bright));
  EXPECT_EQ(night.find(frame, whole, origin).error(), ics::Error::kEmpty);
  EXPECT_EQ(night.settings().polarity, Polarity::bright);
}

TEST(TrackerCentroid, LeavesOutBlobsOutsideTheAreaBounds)
{
  Frame_maker maker(width, height, sky);
  maker.set(spot_x, spot_y, bright);
  ics::tracker::Centroider centroider({Polarity::bright, k_sigma, min_contrast, 2, max_area});
  EXPECT_EQ(centroider.find(maker.frame(ics::UtcTime{}), whole, origin).error(), ics::Error::kEmpty);
  ics::tracker::Centroider small({Polarity::bright, k_sigma, min_contrast, 1, 0});
  EXPECT_EQ(small.find(maker.frame(ics::UtcTime{}), whole, origin).error(), ics::Error::kEmpty);
}

TEST(TrackerCentroid, SearchesOnlyWindowsInTheFrame)
{
  Frame_maker maker(width, height, sky);
  maker.set(spot_x, spot_y, bright);
  const ics::tracker::Frame frame = maker.frame(ics::UtcTime{});
  ics::tracker::Centroider centroider(settings(Polarity::bright));
  const auto refused = std::to_array<Window>({{-1, 0, 5, 5},
                                              {0, -1, 5, 5},
                                              {width - 4, 0, 5, 5},
                                              {0, height - 4, 5, 5},
                                              {0, 0, 2, 5},
                                              {0, 0, 5, 2},
                                              {0, 0, big_side, 5},
                                              {0, 0, 5, big_side}});
  for (const Window& window : refused)
  {
    EXPECT_EQ(centroider.find(frame, window, origin).error(), ics::Error::kInvalidArgument);
  }
}

TEST(TrackerCentroid, CentresWindowsAndKeepsThemInTheFrame)
{
  const ics::Result<Window> middle = ics::tracker::window_around({spot_x + 0.4, spot_y + 0.6}, window_size, frame_size);
  ASSERT_TRUE(middle.has_value());
  EXPECT_EQ(middle->x, spot_x - 2);
  EXPECT_EQ(middle->y, spot_y);
  const ics::Result<Window> corner = ics::tracker::window_around({near_edge, 0.0}, window_size, frame_size);
  ASSERT_TRUE(corner.has_value());
  EXPECT_EQ(corner->x, width - window_size.width);
  EXPECT_EQ(corner->y, 0);
}

TEST(TrackerCentroid, RefusesWindowsThatDoNotFitAndCentresOutside)
{
  const auto too_small = std::to_array<ics::tracker::Extent>({{2, 3}, {3, 2}, {big_side, 3}, {3, big_side}});
  for (const ics::tracker::Extent size : too_small)
  {
    EXPECT_EQ(ics::tracker::window_around(origin, size, {big_frame, big_frame}).error(), ics::Error::kInvalidArgument);
  }
  EXPECT_EQ(ics::tracker::window_around(origin, {width + 1, 3}, frame_size).error(), ics::Error::kInvalidArgument);
  EXPECT_EQ(ics::tracker::window_around(origin, {3, height + 1}, frame_size).error(), ics::Error::kInvalidArgument);
  const float64 nan = std::numeric_limits<float64>::quiet_NaN();
  const auto outside = std::to_array<ics::tracker::Pixel_point>({{-0.5, 1.0},
                                                                 {1.0, -0.5},
                                                                 {static_cast<float64>(width), 1.0},
                                                                 {1.0, static_cast<float64>(height)},
                                                                 {nan, 1.0},
                                                                 {1.0, nan}});
  for (const ics::tracker::Pixel_point centre : outside)
  {
    EXPECT_EQ(ics::tracker::window_around(centre, window_size, frame_size).error(), ics::Error::kOutOfRange);
  }
}
