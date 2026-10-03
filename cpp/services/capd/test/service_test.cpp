#include "ics/capd/service.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <thread>

#include <gtest/gtest.h>
#include <poll.h>

#include "capd_support.hpp"
#include "capture_support.hpp"
#include "ics/capd/config.hpp"
#include "ics/capture/live_capture.hpp"
#include "ics/common/error.hpp"
#include "ics/logging/logger.hpp"
#include "support.hpp"

namespace {

using ics::capd::Config;
using ics::capd::Service;
using ics::capd::testing::Logged;
using ics::capd::testing::pcap_files;
using ics::capd::testing::test_config;
using ics::capture::TimestampSource;
using ics::capture::testing::Loopback;
using ics::timing::testing::TempDir;
using std::chrono::seconds;

// Reads port 0 for half a second, long enough for the kernel to hand over
// what was sent.
void read_for_a_while(Service& service, const Logged& logged) {
  for (int i = 0; i < 5; ++i) {
    pollfd ready{service.fd(0), POLLIN, 0};
    ::poll(&ready, 1, 100);
    ASSERT_TRUE(service.read(0, logged.logger).has_value());
  }
}

std::size_t count(const std::string& text, const std::string_view what) {
  std::size_t found = 0;
  for (std::size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + 1)) {
    ++found;
  }
  return found;
}

TEST(CapdService, CapturesAPortAndLogsEachFile) {
  const TempDir dir;
  Logged logged;
  Service service = Service::open(test_config(dir)).value();
  EXPECT_EQ(service.ports(), 1U);
  EXPECT_EQ(service.utc_shift(), ics::Duration::zero());
  const Loopback loopback;
  loopback.send(5);
  read_for_a_while(service, logged);
  EXPECT_TRUE(service.rotate_due(ics::logging::system_now(), logged.logger).has_value());
  EXPECT_TRUE(logged.out.str().empty());
  ASSERT_TRUE(service.rotate_due(ics::logging::system_now() + seconds(61), logged.logger).has_value());
  EXPECT_EQ(count(logged.out.str(), R"("level":"info","service":"ics-capd","event":"file_closed","interface":"lo")"), 1U);
  EXPECT_NE(logged.out.str().find(R"("dropped":0,"interface_dropped":0})"), std::string::npos);
  EXPECT_EQ(ics::capture::testing::marked(pcap_files(dir)), 5U);
  // Nothing is open now, so closing logs nothing more.
  ASSERT_TRUE(service.close(logged.logger).has_value());
  EXPECT_EQ(count(logged.out.str(), "file_closed"), 1U);
}

TEST(CapdService, LogsFilesFilledWhileReading) {
  const TempDir dir;
  Logged logged;
  Config config = test_config(dir);
  // Room for a few of the test's datagrams per file.
  config.rotation.max_bytes = 512;
  Service service = Service::open(config).value();
  const Loopback loopback;
  loopback.send(20);
  read_for_a_while(service, logged);
  ASSERT_TRUE(service.close(logged.logger).has_value());
  EXPECT_GT(count(logged.out.str(), R"("event":"file_closed")"), 2U);
  EXPECT_EQ(ics::capture::testing::marked(pcap_files(dir)), 20U);
}

TEST(CapdService, LogsDropsAsAnError) {
  const TempDir dir;
  Logged logged;
  Config config = test_config(dir);
  config.ring_bytes = 1U << 16U;
  Service service = Service::open(config).value();
  // Far more than the ring holds, with nothing reading.
  const Loopback loopback;
  loopback.send(20'000);
  for (int i = 0; i < 100; ++i) {
    ASSERT_TRUE(service.read(0, logged.logger).has_value());
  }
  ASSERT_TRUE(service.close(logged.logger).has_value());
  EXPECT_EQ(count(logged.out.str(), R"("level":"error","service":"ics-capd","event":"file_closed")"), 1U);
}

TEST(CapdService, RejectsAPortItDoesNotHave) {
  const TempDir dir;
  Logged logged;
  Service service = Service::open(test_config(dir)).value();
  ::testing::internal::CaptureStderr();
  const ics::Status read = service.read(1, logged.logger);
  EXPECT_NE(::testing::internal::GetCapturedStderr().find("ICS check failed"), std::string::npos);
  ASSERT_FALSE(read.has_value());
  EXPECT_EQ(read.error(), ics::Error::kOutOfRange);
}

TEST(CapdService, ReportsAFileItCannotWrite) {
  const TempDir dir;
  Logged logged;
  Config config = test_config(dir);
  config.output_dir = dir / "gone";
  std::filesystem::create_directory(config.output_dir);
  Service service = Service::open(config).value();
  std::filesystem::remove(config.output_dir);
  const Loopback loopback;
  loopback.send(3);
  ics::Status read;
  const auto deadline = std::chrono::steady_clock::now() + seconds(3);
  while (read.has_value() && std::chrono::steady_clock::now() < deadline) {
    pollfd ready{service.fd(0), POLLIN, 0};
    ::poll(&ready, 1, 100);
    read = service.read(0, logged.logger);
  }
  ASSERT_FALSE(read.has_value());
  EXPECT_EQ(read.error(), ics::Error::kUnwritable);
}

TEST(CapdService, ReportsAFileItCannotFinish) {
  const TempDir dir;
  Logged logged;
  Service service = Service::open(test_config(dir)).value();
  const Loopback loopback;
  loopback.send(5);
  read_for_a_while(service, logged);
  {
    const ics::capture::testing::FileSizeLimit limit(10);
    const ics::Status rotated = service.rotate_due(ics::logging::system_now() + seconds(61), logged.logger);
    ASSERT_FALSE(rotated.has_value());
    EXPECT_EQ(rotated.error(), ics::Error::kUnwritable);
  }
  loopback.send(5);
  read_for_a_while(service, logged);
  const ics::capture::testing::FileSizeLimit limit(10);
  const ics::Status closed = service.close(logged.logger);
  ASSERT_FALSE(closed.has_value());
  EXPECT_EQ(closed.error(), ics::Error::kUnwritable);
}

TEST(CapdService, ReportsPortsItCannotOpen) {
  const TempDir dir;
  Config missing = test_config(dir);
  missing.interfaces = {"ics-none0"};
  EXPECT_EQ(Service::open(missing).error(), ics::Error::kUnavailable);
  Config nowhere = test_config(dir);
  nowhere.output_dir = dir / "missing";
  EXPECT_EQ(Service::open(nowhere).error(), ics::Error::kUnwritable);
}

TEST(CapdService, ReadsTaiMinusUtcFromPtp4lForAdapterTimeStamps) {
  const TempDir dir;
  Config config = test_config(dir);
  config.timestamps = TimestampSource::kAdapter;
  // No ptp4l.
  EXPECT_EQ(Service::open(config).error(), ics::Error::kUnavailable);
  // No room for this end of ptp4l's socket.
  Config no_client = config;
  no_client.client_socket = dir / "missing" / "client";
  EXPECT_EQ(Service::open(no_client).error(), ics::Error::kUnavailable);
  // ptp4l answers with no valid UTC offset: flags 0x38 rather than 0x3c.
  const ics::timing::testing::FakePtp4l ptp4l(config.ptp4l_socket);
  std::string untraceable(ics::timing::testing::kTimeProperties);
  untraceable.replace(untraceable.size() - 4, 2, "38");
  const ics::timing::testing::Answers invalid{ics::timing::testing::kCurrent, ics::timing::testing::kParent,
                                              untraceable, ics::timing::testing::kPort};
  std::thread responder([&ptp4l, &invalid] { ptp4l.respond(invalid); });
  const auto unmarked = Service::open(config);
  responder.join();
  EXPECT_EQ(unmarked.error(), ics::Error::kUnavailable);
  // A valid offset is read; then the loopback interface has no hardware time
  // stamps to give.
  std::thread valid([&ptp4l] { ptp4l.respond(); });
  const auto no_stamps = Service::open(config);
  valid.join();
  EXPECT_EQ(no_stamps.error(), ics::Error::kInvalidArgument);
}

}  // namespace
