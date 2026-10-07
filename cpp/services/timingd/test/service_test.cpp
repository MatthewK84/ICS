#include "ics/timingd/service.hpp"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "ics/common/error.hpp"
#include "ics/logging/json_line.hpp"
#include "ics/logging/logger.hpp"
#include "ics/testing/no_allocation_scope.hpp"
#include "ics/timing/camera_offsets.hpp"
#include "ics/timing/clock_state.hpp"
#include "ics/timingd/config.hpp"
#include "ics/v1/time_quality.pb.h"
#include "support.hpp"
#include "timingd_support.hpp"

namespace {

using ics::timing::ClockState;
using ics::timing::testing::FakePtp4l;
using ics::timing::testing::TempDir;
using ics::timingd::Config;
using ics::timingd::Service;
using ics::timingd::testing::Subscriber;
using ics::v1::TimeQuality;

// How many lines of the log hold text.
std::size_t count(const std::string& log, const std::string_view text) {
  std::size_t found = 0;
  for (std::size_t at = log.find(text); at != std::string::npos; at = log.find(text, at + 1)) {
    ++found;
  }
  return found;
}

struct Rig {
  TempDir dir;
  FakePtp4l ptp4l{dir / "ptp4l-ro"};
  Config config = ics::timingd::testing::test_config(dir);
  Service service = Service::open(config).value();
  std::ostringstream log;
  ics::logging::Logger logger = ics::logging::Logger::to_stream("ics-timingd", ics::logging::Level::kDebug, log,
                                                                &ics::timingd::testing::fixed_clock);
};

TEST(Service, PublishesTheLockedState) {
  Rig rig;
  const Subscriber subscriber(rig.config.publish_socket);
  rig.ptp4l.answer(rig.config.client_socket, 0);
  rig.service.step(rig.logger);
  EXPECT_EQ(rig.service.state(), ClockState::kLocked);
  EXPECT_EQ(rig.service.subscribers(), 1U);
  const std::optional<TimeQuality> report = subscriber.next();
  ASSERT_TRUE(report.has_value());
  EXPECT_EQ(report->station_id(), "station-1");
  EXPECT_GT(report->time_utc_ns(), 0);
  EXPECT_EQ(report->clock_state(), TimeQuality::CLOCK_STATE_LOCKED);
  EXPECT_EQ(report->ptp_offset_ns(), -191);
  EXPECT_EQ(report->ptp_path_delay_ns(), 1455);
  // |offset| + the 100 ns that clockAccuracy 0x21 announces + the asymmetry bound.
  EXPECT_EQ(report->error_bound_ns(), 191 + 100 + 1000);
  EXPECT_EQ(count(rig.log.str(), R"("event":"ptp4l_answering")"), 1U);
  EXPECT_EQ(count(rig.log.str(), R"("event":"clock_state","state":"locked","ptp_offset_ns":-191,)"), 1U);
}

TEST(Service, FlagsHoldoverOnThePollThatShowsIt) {
  Rig rig;
  const Subscriber subscriber(rig.config.publish_socket);
  rig.ptp4l.answer(rig.config.client_socket, 0);
  rig.service.step(rig.logger);
  rig.ptp4l.answer(rig.config.client_socket, 4, ics::timing::testing::kHoldover);
  rig.service.step(rig.logger);
  EXPECT_EQ(rig.service.state(), ClockState::kHoldover);
  ASSERT_TRUE(subscriber.next().has_value());
  const std::optional<TimeQuality> report = subscriber.next();
  ASSERT_TRUE(report.has_value());
  EXPECT_EQ(report->clock_state(), TimeQuality::CLOCK_STATE_HOLDOVER);
  EXPECT_EQ(count(rig.log.str(), R"("event":"clock_state","state":"holdover")"), 1U);
}

TEST(Service, LogsOnlyChanges) {
  Rig rig;
  for (std::uint16_t first = 0; first < 12; first += 4) {
    rig.ptp4l.answer(rig.config.client_socket, first);
    rig.service.step(rig.logger);
  }
  // ptp4l answering, the clock state, and no camera offsets file yet.
  EXPECT_EQ(count(rig.log.str(), "\n"), 3U);
  EXPECT_EQ(count(rig.log.str(), R"("event":"camera_offsets_none")"), 1U);
}

TEST(Service, TreatsASilentPtp4lAsFreeRunning) {
  Rig rig;
  rig.ptp4l.answer(rig.config.client_socket, 0);
  rig.service.step(rig.logger);
  rig.service.step(rig.logger);
  EXPECT_EQ(rig.service.state(), ClockState::kFreeRunning);
  EXPECT_EQ(count(rig.log.str(), R"("level":"warn","service":"ics-timingd","event":"ptp4l_unavailable","error":"unavailable")"),
            1U);
  EXPECT_EQ(count(rig.log.str(), R"("event":"clock_state","state":"free_running")"), 1U);
  rig.ptp4l.answer(rig.config.client_socket, 8);
  rig.service.step(rig.logger);
  EXPECT_EQ(rig.service.state(), ClockState::kLocked);
  EXPECT_EQ(count(rig.log.str(), R"("event":"ptp4l_answering")"), 2U);
}

TEST(Service, StepsWithoutAllocatingOnceSettled) {
  Rig rig;
  const Subscriber subscriber(rig.config.publish_socket);
  rig.ptp4l.answer(rig.config.client_socket, 0);
  rig.service.step(rig.logger);
  rig.ptp4l.answer(rig.config.client_socket, 4);
  {
    const ics::testing::NoAllocationScope no_allocation;
    rig.service.step(rig.logger);
  }
  EXPECT_EQ(rig.service.state(), ClockState::kLocked);
  ASSERT_TRUE(subscriber.next().has_value());
  EXPECT_TRUE(subscriber.next().has_value());
}

TEST(Service, PublishesTheStrobeCalibrationsOffsets) {
  Rig rig;
  const Subscriber subscriber(rig.config.publish_socket);
  rig.ptp4l.answer(rig.config.client_socket, 0);
  rig.service.step(rig.logger);
  EXPECT_EQ(subscriber.next().value().camera_offsets_size(), 0);
  EXPECT_EQ(count(rig.log.str(), R"("event":"camera_offsets_none")"), 1U);
  ics::v1::TimeQuality::CameraOffset offset;
  offset.set_camera_id("phantom-1");
  offset.set_offset_ns(37'400);
  offset.set_offset_sigma_ns(40);
  offset.set_measured_utc_ns(1'791'331'240'000'000'000);
  const ics::timing::CameraOffsets ours{.station_id = "station-1", .offsets = {offset, offset}};
  ASSERT_TRUE(ics::timing::write_camera_offsets(rig.config.camera_offsets_file, ours).has_value());
  rig.ptp4l.answer(rig.config.client_socket, 4);
  rig.service.step(rig.logger);
  const ics::v1::TimeQuality report = subscriber.next().value();
  ASSERT_EQ(report.camera_offsets_size(), 2);
  EXPECT_EQ(report.camera_offsets(0).offset_ns(), 37'400);
  EXPECT_EQ(count(rig.log.str(), R"("event":"camera_offsets","count":2)"), 1U);
  // Unchanged, the file is not read again.
  rig.ptp4l.answer(rig.config.client_socket, 8);
  rig.service.step(rig.logger);
  EXPECT_EQ(subscriber.next().value().camera_offsets_size(), 2);
  EXPECT_EQ(count(rig.log.str(), R"("event":"camera_offsets")"), 1U);
}

TEST(Service, PublishesNoOffsetsFromAFileItCannotUse) {
  Rig rig;
  const Subscriber subscriber(rig.config.publish_socket);
  ics::v1::TimeQuality::CameraOffset offset;
  offset.set_camera_id("phantom-1");
  offset.set_measured_utc_ns(1);
  const ics::timing::CameraOffsets theirs{.station_id = "station-2", .offsets = {offset}};
  ASSERT_TRUE(ics::timing::write_camera_offsets(rig.config.camera_offsets_file, theirs).has_value());
  rig.ptp4l.answer(rig.config.client_socket, 0);
  rig.service.step(rig.logger);
  EXPECT_EQ(subscriber.next().value().camera_offsets_size(), 0);
  EXPECT_EQ(count(rig.log.str(), R"("event":"camera_offsets_unusable","error":"another station's")"), 1U);
  std::ofstream(rig.config.camera_offsets_file, std::ios::trunc) << "not an offsets file";
  std::filesystem::last_write_time(rig.config.camera_offsets_file,
                                   std::filesystem::last_write_time(rig.config.camera_offsets_file) + std::chrono::seconds(1));
  rig.ptp4l.answer(rig.config.client_socket, 4);
  rig.service.step(rig.logger);
  EXPECT_EQ(subscriber.next().value().camera_offsets_size(), 0);
  EXPECT_EQ(count(rig.log.str(), R"("event":"camera_offsets_unusable","error":"malformed")"), 1U);
}

TEST(Service, ReportsSocketsItCannotOpen) {
  const TempDir dir;
  Config config = ics::timingd::testing::test_config(dir);
  config.client_socket = dir / "missing" / "client";
  const ics::Result<Service> no_client = Service::open(config);
  ASSERT_FALSE(no_client.has_value());
  EXPECT_EQ(no_client.error(), ics::Error::kUnavailable);
  config = ics::timingd::testing::test_config(dir);
  config.publish_socket = dir / "missing" / "time-quality";
  const ics::Result<Service> no_publisher = Service::open(config);
  ASSERT_FALSE(no_publisher.has_value());
  EXPECT_EQ(no_publisher.error(), ics::Error::kUnavailable);
  // The client end opened first, and was removed with it.
  EXPECT_FALSE(std::filesystem::exists(config.client_socket));
}

}  // namespace
