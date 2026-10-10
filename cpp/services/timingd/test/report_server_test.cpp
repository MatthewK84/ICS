#include "ics/timingd/report_server.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <grpcpp/grpcpp.h>
#include <gtest/gtest.h>

#include "ics/common/error.hpp"
#include "ics/v1/time_quality.pb.h"
#include "support.hpp"
#include "timingd_support.hpp"

namespace {

using ics::timing::testing::TempDir;
using ics::timingd::ReportServer;
using ics::timingd::testing::report_at;
using ics::timingd::testing::serialized;
using ics::timingd::testing::Subscriber;
using ics::timingd::testing::wait_until;
using ics::v1::TimeQuality;

constexpr std::size_t kRoom = 256;

std::unique_ptr<ReportServer> serve(const std::filesystem::path& socket) {
  std::unique_ptr<ReportServer> server = ReportServer::open(socket).value();
  server->reserve(kRoom);
  return server;
}

void post(ReportServer& server, const std::int64_t time_utc_ns,
          const TimeQuality::ClockState state = TimeQuality::CLOCK_STATE_LOCKED) {
  server.post(serialized(report_at(time_utc_ns, state)));
}

std::optional<std::int64_t> time_of(const std::optional<TimeQuality>& report) {
  return report ? std::optional(report->time_utc_ns()) : std::nullopt;
}

TEST(ReportServer, SendsTheLatestReportThenEachNewOne) {
  const TempDir dir;
  const std::unique_ptr<ReportServer> server = serve(dir / "time-quality");
  post(*server, 1);
  post(*server, 2);
  Subscriber subscriber(dir / "time-quality");
  // Reports posted before it came are skipped, but for the latest.
  EXPECT_EQ(time_of(subscriber.next()), 2);
  EXPECT_EQ(server->subscribers(), 1U);
  post(*server, 3, TimeQuality::CLOCK_STATE_HOLDOVER);
  const std::optional<TimeQuality> report = subscriber.next();
  ASSERT_TRUE(report.has_value());
  EXPECT_EQ(report->time_utc_ns(), 3);
  EXPECT_EQ(report->clock_state(), TimeQuality::CLOCK_STATE_HOLDOVER);
  EXPECT_EQ(report->station_id(), "station-1");
}

TEST(ReportServer, WaitsForTheFirstReport) {
  const TempDir dir;
  const std::unique_ptr<ReportServer> server = serve(dir / "time-quality");
  Subscriber subscriber(dir / "time-quality");
  ASSERT_TRUE(wait_until([&server] { return server->subscribers() == 1; }));
  // Its thread wakes a few times with nothing to send.
  std::this_thread::sleep_for(3 * ReportServer::kWakeInterval);
  post(*server, 1);
  EXPECT_EQ(time_of(subscriber.next()), 1);
}

TEST(ReportServer, RefusesSubscribersPastTheLimit) {
  const TempDir dir;
  const std::unique_ptr<ReportServer> server = serve(dir / "time-quality");
  post(*server, 1);
  std::vector<std::unique_ptr<Subscriber>> watching;
  for (std::size_t count = 0; count < ReportServer::kMaxSubscribers; ++count) {
    watching.push_back(std::make_unique<Subscriber>(dir / "time-quality"));
    ASSERT_EQ(time_of(watching.back()->next()), 1);
  }
  EXPECT_EQ(server->subscribers(), ReportServer::kMaxSubscribers);
  Subscriber refused(dir / "time-quality");
  EXPECT_FALSE(refused.next().has_value());
  EXPECT_EQ(refused.finish(), grpc::StatusCode::RESOURCE_EXHAUSTED);
  // One leaves, and another can come.
  watching.pop_back();
  ASSERT_TRUE(wait_until([&server] { return server->subscribers() < ReportServer::kMaxSubscribers; }));
  Subscriber next(dir / "time-quality");
  EXPECT_EQ(time_of(next.next()), 1);
}

TEST(ReportServer, NoticesAClientThatLeavesWithoutAnotherReport) {
  const TempDir dir;
  const std::unique_ptr<ReportServer> server = serve(dir / "time-quality");
  post(*server, 1);
  Subscriber subscriber(dir / "time-quality");
  ASSERT_EQ(time_of(subscriber.next()), 1);
  subscriber.cancel();
  EXPECT_TRUE(wait_until([&server] { return server->subscribers() == 0; }));
}

TEST(ReportServer, StopsWritingToAClientThatLeft) {
  const TempDir dir;
  const std::unique_ptr<ReportServer> server = serve(dir / "time-quality");
  post(*server, 1);
  Subscriber subscriber(dir / "time-quality");
  ASSERT_EQ(time_of(subscriber.next()), 1);
  subscriber.cancel();
  // Reports keep coming, faster than its thread would wake without them, so
  // it notices when a write fails.
  std::int64_t time_utc_ns = 2;
  EXPECT_TRUE(wait_until([&server, &time_utc_ns] {
    post(*server, time_utc_ns++);
    return server->subscribers() == 0;
  }));
}

TEST(ReportServer, EndsEveryStreamWhenItStops) {
  const TempDir dir;
  std::unique_ptr<ReportServer> server = serve(dir / "time-quality");
  post(*server, 1);
  Subscriber subscriber(dir / "time-quality");
  ASSERT_EQ(time_of(subscriber.next()), 1);
  server.reset();
  EXPECT_FALSE(subscriber.next().has_value());
  EXPECT_EQ(subscriber.finish(), grpc::StatusCode::OK);
  EXPECT_FALSE(std::filesystem::exists(dir / "time-quality"));
}

TEST(ReportServer, ReplacesAFileLeftAtItsSocket) {
  const TempDir dir;
  std::ofstream(dir / "time-quality") << "left over";
  const std::unique_ptr<ReportServer> server = serve(dir / "time-quality");
  post(*server, 1);
  Subscriber subscriber(dir / "time-quality");
  EXPECT_EQ(time_of(subscriber.next()), 1);
}

TEST(ReportServer, RefusesASocketItCannotMake) {
  const TempDir dir;
  const ics::Result<std::unique_ptr<ReportServer>> too_long = ReportServer::open(dir / std::string(200, 'x'));
  ASSERT_FALSE(too_long.has_value());
  EXPECT_EQ(too_long.error(), ics::Error::kInvalidArgument);
  const ics::Result<std::unique_ptr<ReportServer>> no_folder = ReportServer::open(dir / "missing" / "time-quality");
  ASSERT_FALSE(no_folder.has_value());
  EXPECT_EQ(no_folder.error(), ics::Error::kUnavailable);
}

}  // namespace
