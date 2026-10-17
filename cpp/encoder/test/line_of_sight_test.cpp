// Each frame's line of sight at mid-exposure, and the frames refused.
#include "ics/encoder/line_of_sight.hpp"

#include <array>
#include <cstdint>
#include <limits>

#include <gtest/gtest.h>

#include "encoder_support.hpp"
#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/encoder/pointing_model.hpp"
#include "ics/encoder/track.hpp"
#include "ics/v1/camera_frame_meta.pb.h"

namespace {

using ics::encoder::AngleTrack;
using ics::encoder::Coefficient;
using ics::encoder::PointingModel;
using ics::encoder::Term;
using ics::v1::CameraFrameMeta;
using namespace ics::encoder::testing;

constexpr std::int64_t kExposureNs = 2'000'000;

[[nodiscard]] AngleTrack moving_track() {
  AngleTrack track = AngleTrack::make(config()).value();
  for (std::uint32_t i = 0; i < 3; ++i) {
    track.add(batch({.sequence = i,
                     .first_ns = kStartNs + (static_cast<std::int64_t>(i) * kBatchNs),
                     .azimuth = 1000 + (static_cast<std::int32_t>(i) * 1000),
                     .azimuth_step = 100,
                     .elevation = 1 << 21,
                     .elevation_step = 0}));
  }
  return track;
}

[[nodiscard]] PointingModel model() {
  const std::array<Coefficient, 2> terms{{{Term::kIA, 1e-3}, {Term::kTF, 2e-4}}};
  return PointingModel::make(terms).value();
}

[[nodiscard]] CameraFrameMeta frame(const std::int64_t start_ns, const std::int64_t duration_ns = kExposureNs) {
  CameraFrameMeta meta;
  meta.set_frame_index(42);
  meta.set_exposure_start_utc_ns(start_ns);
  meta.set_exposure_duration_ns(duration_ns);
  meta.set_time_source(CameraFrameMeta::TIME_SOURCE_IRIG);
  return meta;
}

TEST(LineOfSight, AppliesTheModelAtMidExposure) {
  const AngleTrack track = moving_track();
  const auto seen = ics::encoder::line_of_sight(track, model(), frame(kStartNs + (4 * kSampleNs)));
  ASSERT_TRUE(seen.has_value());
  EXPECT_EQ(seen->frame_index, 42U);
  EXPECT_EQ(seen->mid_exposure, ics::utc_from_ns(kStartNs + (5 * kSampleNs)));
  EXPECT_NEAR(seen->axes.encoder.azimuth_rad, 1500 * kCountRad, 1e-12);
  const ics::encoder::Direction expected = ics::encoder::to_sky(model(), seen->axes.encoder);
  EXPECT_EQ(seen->sky.azimuth_rad, expected.azimuth_rad);
  EXPECT_EQ(seen->sky.elevation_rad, expected.elevation_rad);
  EXPECT_GT(seen->axes.bound_rad, 0.0);
}

TEST(LineOfSight, TakesOnlyFramesTimedByIrigB) {
  const AngleTrack track = moving_track();
  CameraFrameMeta host = frame(kStartNs + kBatchNs);
  host.set_time_source(CameraFrameMeta::TIME_SOURCE_HOST);
  EXPECT_EQ(ics::encoder::line_of_sight(track, model(), host).error(), ics::Error::kUnconstrained);
  host.set_time_source(CameraFrameMeta::TIME_SOURCE_UNSPECIFIED);
  EXPECT_EQ(ics::encoder::line_of_sight(track, model(), host).error(), ics::Error::kUnconstrained);
}

TEST(LineOfSight, RefusesAnExposureOfNegativeLengthOrPastTheEndOfTime) {
  const AngleTrack track = moving_track();
  EXPECT_EQ(ics::encoder::line_of_sight(track, model(), frame(kStartNs + kBatchNs, -2)).error(),
            ics::Error::kInvalidArgument);
  const std::int64_t last = std::numeric_limits<std::int64_t>::max();
  EXPECT_EQ(ics::encoder::line_of_sight(track, model(), frame(last - 1, 4)).error(), ics::Error::kInvalidArgument);
  EXPECT_EQ(ics::encoder::line_of_sight(track, model(), frame(last - 2, 4)).error(), ics::Error::kOutOfRange);
}

TEST(LineOfSight, RefusesAFrameOutsideTheHistory) {
  const AngleTrack track = moving_track();
  EXPECT_EQ(ics::encoder::line_of_sight(track, model(), frame(kStartNs - kExposureNs)).error(),
            ics::Error::kOutOfRange);
  EXPECT_EQ(ics::encoder::line_of_sight(track, model(), frame(kStartNs + (3 * kBatchNs))).error(),
            ics::Error::kOutOfRange);
}

}  // namespace
