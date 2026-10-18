// measurement_test.cpp: PLI records as reports in the range frame, their default sigmas and their refusals (ICS-037).
#include <limits>

#include <gtest/gtest.h>

#include <ics/common/error.hpp>
#include <ics/common/units.hpp>
#include <ics/common/universal_types.h>
#include <ics/cue/measurement.h>
#include <ics/frames/enu.hpp>
#include <ics/frames/geodetic.hpp>
#include <ics/v1/pli.pb.h>

namespace
{

using ics::float64;
using ics::cue::Fix_sigmas;
using ics::v1::PliRecord;

constexpr float64 origin_latitude_deg = 40.0;
constexpr float64 origin_longitude_deg = -100.0;
constexpr float64 origin_height_m = 700.0;
constexpr float64 north_deg = 0.001;
constexpr float64 valid_ns = 1.7E18;
constexpr float64 close_m = 1.0E-6;
constexpr ics::cue::Noise_defaults defaults{{3.0, 9.0}, {1.0, 2.0}, {0.6, 1.1}, {0.3, 0.6}, {0.02, 0.04}, {4.0, 4.5}, 0.25};

ics::frames::EnuFrame range()
{
  const ics::Result<ics::frames::Geodetic> origin = ics::frames::Geodetic::make(
    ics::Degrees(origin_latitude_deg), ics::Degrees(origin_longitude_deg), ics::Meters(origin_height_m));
  EXPECT_TRUE(origin.has_value());
  return ics::frames::EnuFrame(*origin);
}

PliRecord record(const PliRecord::FixType fix_type)
{
  PliRecord made;
  made.set_valid_utc_ns(static_cast<ics::int64>(valid_ns));
  made.set_fix_type(fix_type);
  made.mutable_position()->set_latitude_deg(origin_latitude_deg + north_deg);
  made.mutable_position()->set_longitude_deg(origin_longitude_deg);
  made.mutable_position()->set_height_ellipsoid_m(origin_height_m);
  return made;
}

}  // namespace

// JSF deviation D-5: GoogleTest's TEST and EXPECT macros fix the form of the
// test names and expand to code of GoogleTest's own (AV 50, 51 and 209).
TEST(CueMeasurement, TakesEachFixTypesDefaults)
{
  EXPECT_DOUBLE_EQ(ics::cue::defaults_for(PliRecord::FIX_TYPE_TWO_DIMENSIONAL, defaults).vertical_m, 9.0);
  EXPECT_DOUBLE_EQ(ics::cue::defaults_for(PliRecord::FIX_TYPE_THREE_DIMENSIONAL, defaults).vertical_m, 2.0);
  EXPECT_DOUBLE_EQ(ics::cue::defaults_for(PliRecord::FIX_TYPE_DGNSS, defaults).vertical_m, 1.1);
  EXPECT_DOUBLE_EQ(ics::cue::defaults_for(PliRecord::FIX_TYPE_RTK_FLOAT, defaults).vertical_m, 0.6);
  EXPECT_DOUBLE_EQ(ics::cue::defaults_for(PliRecord::FIX_TYPE_RTK_FIXED, defaults).vertical_m, 0.04);
  EXPECT_DOUBLE_EQ(ics::cue::defaults_for(PliRecord::FIX_TYPE_OTHER, defaults).vertical_m, 4.5);
  EXPECT_DOUBLE_EQ(ics::cue::defaults_for(PliRecord::FIX_TYPE_UNSPECIFIED, defaults).horizontal_m, 4.0);
}

TEST(CueMeasurement, PutsARecordInTheRangeFrame)
{
  const ics::Result<ics::cue::Measurement> report =
    ics::cue::measurement_from(record(PliRecord::FIX_TYPE_THREE_DIMENSIONAL), range(), defaults);
  ASSERT_TRUE(report.has_value());
  EXPECT_NEAR(report->position[0], 0.0, close_m);
  EXPECT_NEAR(report->position[1], 111.04, 0.01);
  EXPECT_DOUBLE_EQ(report->horizontal_sigma_m, 1.0);
  EXPECT_DOUBLE_EQ(report->vertical_sigma_m, 2.0);
  EXPECT_FALSE(report->has_velocity);
  EXPECT_DOUBLE_EQ(report->velocity_sigma_m_s, 0.25);
  EXPECT_EQ(report->time.time_since_epoch().count(), static_cast<ics::int64>(valid_ns));
}

TEST(CueMeasurement, KeepsARecordsOwnSigmasAndVelocity)
{
  PliRecord moving = record(PliRecord::FIX_TYPE_RTK_FIXED);
  moving.set_horizontal_sigma_m(0.05);
  moving.set_vertical_sigma_m(0.07);
  moving.mutable_velocity_enu_mps()->set_north(5.0);
  const ics::Result<ics::cue::Measurement> report = ics::cue::measurement_from(moving, range(), defaults);
  ASSERT_TRUE(report.has_value());
  EXPECT_DOUBLE_EQ(report->horizontal_sigma_m, 0.05);
  EXPECT_DOUBLE_EQ(report->vertical_sigma_m, 0.07);
  ASSERT_TRUE(report->has_velocity);
  EXPECT_NEAR(report->velocity[1], 5.0, 1.0E-6);
  // The vehicle's up leans north of the origin's by about 17 microradians at 111 m.
  EXPECT_NEAR(report->velocity[2], -5.0 * 1.74E-5, 1.0E-6);
}

TEST(CueMeasurement, RefusesWhatCannotBeUsed)
{
  EXPECT_EQ(ics::cue::measurement_from(record(PliRecord::FIX_TYPE_NONE), range(), defaults).error(),
            ics::Error::kUnconstrained);
  PliRecord far = record(PliRecord::FIX_TYPE_THREE_DIMENSIONAL);
  far.mutable_position()->set_latitude_deg(91.0);
  EXPECT_EQ(ics::cue::measurement_from(far, range(), defaults).error(), ics::Error::kOutOfRange);
  PliRecord lost = record(PliRecord::FIX_TYPE_THREE_DIMENSIONAL);
  lost.mutable_position()->set_height_ellipsoid_m(std::numeric_limits<float64>::quiet_NaN());
  EXPECT_EQ(ics::cue::measurement_from(lost, range(), defaults).error(), ics::Error::kInvalidArgument);
  PliRecord untimed = record(PliRecord::FIX_TYPE_THREE_DIMENSIONAL);
  untimed.set_valid_utc_ns(0);
  EXPECT_EQ(ics::cue::measurement_from(untimed, range(), defaults).error(), ics::Error::kMalformed);
  PliRecord unsure = record(PliRecord::FIX_TYPE_THREE_DIMENSIONAL);
  unsure.set_vertical_sigma_m(0.0);
  EXPECT_EQ(ics::cue::measurement_from(unsure, range(), defaults).error(), ics::Error::kMalformed);
  PliRecord wild = record(PliRecord::FIX_TYPE_THREE_DIMENSIONAL);
  wild.mutable_velocity_enu_mps()->set_east(std::numeric_limits<float64>::infinity());
  EXPECT_EQ(ics::cue::measurement_from(wild, range(), defaults).error(), ics::Error::kMalformed);
  ics::cue::Noise_defaults loose = defaults;
  loose.velocity_sigma_m_s = -1.0;
  EXPECT_EQ(ics::cue::measurement_from(record(PliRecord::FIX_TYPE_THREE_DIMENSIONAL), range(), loose).error(),
            ics::Error::kMalformed);
  loose = defaults;
  loose.three_dimensional.horizontal_m = std::numeric_limits<float64>::quiet_NaN();
  EXPECT_EQ(ics::cue::measurement_from(record(PliRecord::FIX_TYPE_THREE_DIMENSIONAL), range(), loose).error(),
            ics::Error::kMalformed);
}
