// extrapolator_test.cpp: roles, counts, latency and refusals of the cue extrapolator (ICS-037).
#include <chrono>
#include <limits>

#include <gtest/gtest.h>

#include <ics/common/error.hpp>
#include <ics/common/units.hpp>
#include <ics/common/universal_types.h>
#include <ics/cue/extrapolator.h>
#include <ics/cue/filter.h>
#include <ics/cue/measurement.h>
#include <ics/cue/pointing.h>
#include <ics/v1/common.pb.h>
#include <ics/v1/pli.pb.h>

namespace
{

using ics::float64;
using ics::int64;
using ics::cue::Take;
using ics::v1::PliRecord;

constexpr int64 start_ns = 1700000000000000000;
constexpr int64 period_ns = 100000000;
constexpr int64 reports = 30;
constexpr int64 next_report = 3;
constexpr int64 coasted_report = 60;
constexpr int64 much_later_report = 50;
// About 85 km to a degree of longitude at 40 degrees north.
constexpr float64 metres_per_degree_east = 111000.0 * 0.766;
constexpr float64 start_east_m = -60.0;
constexpr float64 target_height_m = 735.0;
constexpr float64 speed_m_s = 5.0;
constexpr float64 beyond_pole_deg = 95.0;
constexpr float64 far_height_m = 2000.0;
constexpr ics::cue::Site origin{40.0, -100.0, 700.0};
constexpr ics::cue::Site south_station{39.9977477, -100.0, 702.0};
constexpr ics::cue::Fix_sigmas gnss{0.5, 0.8};
constexpr ics::cue::Filter_settings filter{
  {4.0, 1.0}, 16.27, std::chrono::seconds(2), std::chrono::seconds(2), ics::cue::max_history, 20.0, 10.0};
constexpr ics::cue::Noise_defaults defaults{gnss, gnss, gnss, gnss, gnss, gnss, 0.2};
constexpr ics::Duration latency = std::chrono::milliseconds(50);

ics::cue::Cue_settings settings()
{
  return {origin, south_station, ics::v1::ENTITY_ROLE_TARGET, latency, filter, defaults};
}

// The target flying east at 5 m/s, 35 m up, at report number.
PliRecord report(const int64 number, const ics::v1::EntityRole role)
{
  PliRecord made;
  made.set_role(role);
  made.set_valid_utc_ns(start_ns + (number * period_ns));
  made.set_fix_type(PliRecord::FIX_TYPE_THREE_DIMENSIONAL);
  const float64 east_m = start_east_m + (0.5 * static_cast<float64>(number));
  made.mutable_position()->set_latitude_deg(origin.latitude_deg);
  made.mutable_position()->set_longitude_deg(origin.longitude_deg + (east_m / metres_per_degree_east));
  made.mutable_position()->set_height_ellipsoid_m(target_height_m);
  made.mutable_velocity_enu_mps()->set_east(speed_m_s);
  return made;
}

ics::UtcTime at_report(const int64 number)
{
  return ics::UtcTime{} + ics::Duration(start_ns + (number * period_ns));
}

}  // namespace

// JSF deviation D-5: GoogleTest's TEST and EXPECT macros fix the form of the
// test names and expand to code of GoogleTest's own (AV 50, 51 and 209).
TEST(CueExtrapolator, RefusesASiteThatIsNoPoint)
{
  ics::cue::Cue_settings bad = settings();
  bad.range_origin.latitude_deg = beyond_pole_deg;
  EXPECT_EQ(ics::cue::Cue_extrapolator::make(bad).error(), ics::Error::kOutOfRange);
  bad = settings();
  bad.station.height_m = std::numeric_limits<float64>::quiet_NaN();
  EXPECT_EQ(ics::cue::Cue_extrapolator::make(bad).error(), ics::Error::kInvalidArgument);
}

TEST(CueExtrapolator, CuesTheTargetForWhenTheCommandTakesEffect)
{
  ics::Result<ics::cue::Cue_extrapolator> made = ics::cue::Cue_extrapolator::make(settings());
  ASSERT_TRUE(made.has_value());
  ics::cue::Cue_extrapolator& cue = *made;
  EXPECT_EQ(cue.cue_at(at_report(0)).error(), ics::Error::kEmpty);
  for (int64 number = 0; number < reports; ++number)
  {
    EXPECT_EQ(cue.take(report(number, ics::v1::ENTITY_ROLE_TARGET)), Take::taken);
  }
  const ics::Result<ics::cue::Cue> now = cue.cue_at(at_report(reports - 1));
  ASSERT_TRUE(now.has_value());
  EXPECT_EQ(now->time, at_report(reports - 1) + latency);
  EXPECT_GT(now->azimuth_rate_rad_s, 0.0);
  EXPECT_EQ(cue.cue_at(at_report(coasted_report)).error(), ics::Error::kUnconstrained);
}

TEST(CueExtrapolator, CountsEveryFate)
{
  ics::Result<ics::cue::Cue_extrapolator> made = ics::cue::Cue_extrapolator::make(settings());
  ASSERT_TRUE(made.has_value());
  ics::cue::Cue_extrapolator& cue = *made;
  EXPECT_EQ(cue.take(report(1, ics::v1::ENTITY_ROLE_TARGET)), Take::taken);
  EXPECT_EQ(cue.take(report(0, ics::v1::ENTITY_ROLE_TARGET)), Take::late);
  EXPECT_EQ(cue.take(report(2, ics::v1::ENTITY_ROLE_INTERCEPTOR)), Take::not_target);
  PliRecord unfixed = report(2, ics::v1::ENTITY_ROLE_TARGET);
  unfixed.set_fix_type(PliRecord::FIX_TYPE_NONE);
  EXPECT_EQ(cue.take(unfixed), Take::unusable);
  PliRecord wrong = report(next_report, ics::v1::ENTITY_ROLE_TARGET);
  wrong.mutable_position()->set_height_ellipsoid_m(far_height_m);
  EXPECT_EQ(cue.take(wrong), Take::gated);
  EXPECT_EQ(cue.take(report(much_later_report, ics::v1::ENTITY_ROLE_TARGET)), Take::taken);
  EXPECT_EQ(cue.take(report(next_report + 1, ics::v1::ENTITY_ROLE_TARGET)), Take::too_old);
  const ics::cue::Counts counts = cue.counts();
  EXPECT_EQ(counts.taken, 2);
  EXPECT_EQ(counts.late, 1);
  EXPECT_EQ(counts.gated, 1);
  EXPECT_EQ(counts.too_old, 1);
  EXPECT_EQ(counts.not_target, 1);
  EXPECT_EQ(counts.unusable, 1);
}

TEST(CueExtrapolator, RefusesACueWithNoDirection)
{
  ics::cue::Cue_settings above = settings();
  above.station = {origin.latitude_deg, origin.longitude_deg + (start_east_m / metres_per_degree_east), origin.height_m};
  ics::Result<ics::cue::Cue_extrapolator> made = ics::cue::Cue_extrapolator::make(above);
  ASSERT_TRUE(made.has_value());
  PliRecord hovering = report(0, ics::v1::ENTITY_ROLE_TARGET);
  hovering.mutable_velocity_enu_mps()->set_east(0.0);
  EXPECT_EQ(made->take(hovering), Take::taken);
  EXPECT_EQ(made->cue_at(at_report(0)).error(), ics::Error::kUnconstrained);
}
