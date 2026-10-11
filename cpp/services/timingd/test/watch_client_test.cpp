#include "ics/timingd/watch_client.hpp"

#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "ics/timingd/report_server.hpp"
#include "ics/timingd/run.hpp"
#include "ics/v1/time_quality.pb.h"
#include "support.hpp"
#include "timingd_support.hpp"

namespace {

using ics::timing::testing::TempDir;
using ics::timingd::ReportServer;
using ics::timingd::run_watch_client;
using ics::timingd::testing::report_at;
using ics::timingd::testing::serialized;
using ics::timingd::testing::wait_until;
using ics::v1::TimeQuality;

// What a run of ics-time-watch wrote, and its exit status.
struct Ran {
  int status = 0;
  std::string out;
  std::string err;
};

Ran watch(const std::vector<const char*>& args) {
  ::testing::internal::CaptureStdout();
  ::testing::internal::CaptureStderr();
  const int status = run_watch_client(args, stdout);
  std::fflush(stdout);
  std::string out = ::testing::internal::GetCapturedStdout();
  return Ran{.status = status, .out = std::move(out), .err = ::testing::internal::GetCapturedStderr()};
}

std::unique_ptr<ReportServer> serve_holdover(const std::filesystem::path& socket) {
  std::unique_ptr<ReportServer> server = ReportServer::open(socket).value();
  server->reserve(256);
  server->post(serialized(report_at(1'790'000'000'123'456'789, TimeQuality::CLOCK_STATE_HOLDOVER)));
  return server;
}

TEST(WatchClient, PrintsUsageForBadArguments) {
  const std::vector<std::vector<const char*>> bad{
      {"ics-time-watch", "socket", "0"},
      {"ics-time-watch", "socket", "x"},
      {"ics-time-watch", "socket", "2x"},
      {"ics-time-watch", "socket", "1", "more"},
  };
  for (const std::vector<const char*>& args : bad) {
    const Ran ran = watch(args);
    EXPECT_EQ(ran.status, ics::timingd::kExitUsage);
    EXPECT_EQ(ran.err, "usage: ics-time-watch [SOCKET [COUNT]]\n");
    EXPECT_EQ(ran.out, "");
  }
}

TEST(WatchClient, WritesEachReportAsAJsonLine) {
  const TempDir dir;
  const std::unique_ptr<ReportServer> server = serve_holdover(dir / "time-quality");
  ASSERT_EQ(server->subscribers(), 0U);
  const std::string socket = (dir / "time-quality").native();
  const Ran ran = watch({"ics-time-watch", socket.c_str(), "1"});
  EXPECT_EQ(ran.status, 0);
  EXPECT_EQ(ran.out,
            R"({"station_id":"station-1","time_utc_ns":"1790000000123456789","clock_state":"CLOCK_STATE_HOLDOVER",)"
            R"("holdover_duration_ns":"0","gnss_satellite_count":0,"ptp_offset_ns":"0","ptp_path_delay_ns":"0",)"
            R"("irig_b_locked":false,"error_bound_ns":"0","camera_offsets":[]})"
            "\n");
  EXPECT_EQ(ran.err, "");
}

TEST(WatchClient, StopsWhenTheServerEndsTheStream) {
  const TempDir dir;
  std::unique_ptr<ReportServer> server = serve_holdover(dir / "time-quality");
  const std::string socket = (dir / "time-quality").native();
  const std::vector<const char*> args{"ics-time-watch", socket.c_str()};
  int status = -1;
  ::testing::internal::CaptureStdout();
  std::thread watcher([&args, &status] { status = run_watch_client(args, stdout); });
  EXPECT_TRUE(wait_until([&server] { return server->subscribers() == 1; }));
  server.reset();
  watcher.join();
  std::fflush(stdout);
  const std::string out = ::testing::internal::GetCapturedStdout();
  EXPECT_EQ(status, 0);
  EXPECT_NE(out.find(R"("clock_state":"CLOCK_STATE_HOLDOVER")"), std::string::npos) << out;
}

TEST(WatchClient, SaysWhenNoServerAnswers) {
  const TempDir dir;
  const std::string socket = (dir / "time-quality").native();
  // With or without a count: the stream ends before any report.
  for (const char* count : {"", "1"}) {
    const std::vector<const char*> args =
        *count == '\0' ? std::vector{"ics-time-watch", socket.c_str()} : std::vector{"ics-time-watch", socket.c_str(), count};
    const Ran ran = watch(args);
    EXPECT_EQ(ran.status, ics::timingd::kExitUnavailable);
    EXPECT_NE(ran.err.find("ics-time-watch: " + socket + ": "), std::string::npos) << ran.err;
    EXPECT_EQ(ran.out, "");
  }
}

TEST(WatchClient, WatchesTheDefaultSocketWithoutArguments) {
  const Ran ran = watch({"ics-time-watch"});
  EXPECT_EQ(ran.status, ics::timingd::kExitUnavailable);
  EXPECT_NE(ran.err.find("ics-time-watch: /run/ics-timingd/time-quality: "), std::string::npos) << ran.err;
}

}  // namespace
