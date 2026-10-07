#include "ics/timingd/report.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include "ics/common/units.hpp"
#include "ics/testing/no_allocation_scope.hpp"
#include "ics/timing/clock_state.hpp"
#include "ics/timing/quality.hpp"
#include "ics/v1/time_quality.pb.h"

namespace {

using ics::timing::ClockState;
using ics::timing::Quality;
using ics::v1::TimeQuality;
using std::chrono::nanoseconds;

constexpr std::int64_t kUtcNs = 1'790'000'000'123'456'789;

TEST(Report, MapsEachClockState) {
  EXPECT_EQ(ics::timingd::to_proto(ClockState::kLocked), TimeQuality::CLOCK_STATE_LOCKED);
  EXPECT_EQ(ics::timingd::to_proto(ClockState::kHoldover), TimeQuality::CLOCK_STATE_HOLDOVER);
  EXPECT_EQ(ics::timingd::to_proto(ClockState::kFreeRunning), TimeQuality::CLOCK_STATE_FREE_RUNNING);
  EXPECT_EQ(ics::timingd::to_proto(static_cast<ClockState>(0)), TimeQuality::CLOCK_STATE_UNSPECIFIED);
}

TEST(Report, FillsThePerPollFields) {
  TimeQuality report;
  report.set_station_id("station-1");
  const Quality holdover{ClockState::kHoldover, std::chrono::seconds(2), nanoseconds(-191), nanoseconds(1455),
                         nanoseconds(101'291)};
  ics::timingd::fill(holdover, ics::utc_from_ns(kUtcNs), report);
  EXPECT_EQ(report.station_id(), "station-1");
  EXPECT_EQ(report.time_utc_ns(), kUtcNs);
  EXPECT_EQ(report.clock_state(), TimeQuality::CLOCK_STATE_HOLDOVER);
  EXPECT_EQ(report.holdover_duration_ns(), 2'000'000'000);
  EXPECT_EQ(report.ptp_offset_ns(), -191);
  EXPECT_EQ(report.ptp_path_delay_ns(), 1455);
  EXPECT_EQ(report.error_bound_ns(), 101'291);
  EXPECT_EQ(report.gnss_satellite_count(), 0U);
  EXPECT_FALSE(report.irig_b_locked());
  EXPECT_EQ(report.camera_offsets_size(), 0);
}

TEST(Report, WritesAnUnboundedErrorAsTheLargestInteger) {
  TimeQuality report;
  ics::timingd::fill(Quality{}, ics::utc_from_ns(kUtcNs), report);
  EXPECT_EQ(report.clock_state(), TimeQuality::CLOCK_STATE_FREE_RUNNING);
  EXPECT_EQ(report.error_bound_ns(), std::numeric_limits<std::int64_t>::max());
}

TEST(Report, SizesTheLargestReport) {
  // Eight fields of a tag and up to 10 bytes; the station takes a tag, a
  // length and its text.
  TimeQuality report;
  EXPECT_EQ(ics::timingd::max_report_size(report), 65U);
  report.set_station_id("station-1");
  EXPECT_EQ(ics::timingd::max_report_size(report), 76U);
  const Quality extreme{ClockState::kFreeRunning, nanoseconds::min(), nanoseconds::min(), nanoseconds::min(),
                        nanoseconds::min()};
  ics::timingd::fill(extreme, ics::utc_from_ns(std::numeric_limits<std::int64_t>::min()), report);
  EXPECT_LE(report.ByteSizeLong(), ics::timingd::max_report_size(report));
  // Camera offsets add their own length.
  TimeQuality::CameraOffset* offset = report.add_camera_offsets();
  offset->set_camera_id("phantom-1");
  offset->set_offset_ns(-37'400);
  offset->set_offset_sigma_ns(40);
  offset->set_measured_utc_ns(std::numeric_limits<std::int64_t>::max());
  EXPECT_EQ(ics::timingd::max_report_size(report), 76U + 2U + offset->ByteSizeLong());
  EXPECT_LE(report.ByteSizeLong(), ics::timingd::max_report_size(report));
}

TEST(Report, SerializesWithoutAllocating) {
  TimeQuality report;
  report.set_station_id("station-1");
  ics::timingd::fill(Quality{}, ics::utc_from_ns(kUtcNs), report);
  std::vector<std::byte> buffer(ics::timingd::max_report_size(report));
  std::span<const std::byte> written;
  {
    const ics::testing::NoAllocationScope no_allocation;
    written = ics::timingd::serialize(report, buffer);
  }
  TimeQuality read;
  ASSERT_TRUE(read.ParseFromArray(written.data(), static_cast<int>(written.size())));
  EXPECT_EQ(read.station_id(), "station-1");
  EXPECT_EQ(read.time_utc_ns(), kUtcNs);
  EXPECT_EQ(read.error_bound_ns(), std::numeric_limits<std::int64_t>::max());
}

TEST(Report, WritesNothingIntoABufferTooSmall) {
  TimeQuality report;
  report.set_station_id("station-1");
  std::vector<std::byte> buffer(3);
  EXPECT_TRUE(ics::timingd::serialize(report, buffer).empty());
}

}  // namespace
