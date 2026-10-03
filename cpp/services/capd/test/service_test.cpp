#include "ics/capd/service.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "capd_support.hpp"
#include "capture_support.hpp"
#include "ics/capture/capture.hpp"
#include "ics/capture/rotating_writer.hpp"
#include "support.hpp"

namespace {

using ics::capd::Port;
using ics::capd::Service;
using ics::capd::testing::Logged;
using ics::capture::Capture;
using ics::capture::RotatingWriter;
using ics::capture::RotationLimits;
using ics::timing::testing::TempDir;
using std::chrono::seconds;

const ics::UtcTime kStart = ics::utc_from_ns(1'791'047'700'000'000'000);
constexpr RotationLimits kHour{std::chrono::hours(1), 1U << 30U};

// A pcap file of count 100-byte packets in folder/source, to replay.
std::filesystem::path recorded(const std::filesystem::path& folder, const int count) {
  std::filesystem::create_directories(folder / "source");
  RotatingWriter writer = RotatingWriter::open(folder / "source", "rec", {65535, 1}, kHour, kStart).value();
  const std::vector<std::byte> bytes(100, std::byte{9});
  for (int i = 0; i < count; ++i) {
    EXPECT_TRUE(writer.write({kStart, 100, bytes}, kStart).has_value());
  }
  return writer.close().value().path;
}

// A port named name replaying the file at source into folder.
Port replay(const std::string& name, const std::filesystem::path& source, const std::filesystem::path& folder,
            const RotationLimits limits) {
  std::string reason;
  Capture capture = Capture::open_file(source, reason).value();
  RotatingWriter writer = RotatingWriter::open(folder, name, capture.format(), limits, kStart).value();
  return Port{name, std::move(capture), std::move(writer), {}};
}

Service replaying(const std::filesystem::path& source, const std::filesystem::path& folder,
                  const RotationLimits limits) {
  std::vector<Port> ports;
  ports.push_back(replay("tap0", source, folder, limits));
  return Service(std::move(ports));
}

TEST(Service, WritesEveryPacketAndHashesTheFileWhenClosed) {
  const TempDir dir;
  Service service = replaying(recorded(dir / "", 5000), dir / "", kHour);
  EXPECT_EQ(service.descriptors().size(), 1U);
  Logged logged;
  // 16 batches of 256 packets in the first step, the rest in the next.
  ASSERT_TRUE(service.step(kStart, logged.logger).has_value());
  ASSERT_TRUE(service.step(kStart, logged.logger).has_value());
  ASSERT_TRUE(service.close(logged.logger).has_value());
  EXPECT_TRUE(logged.has(R"("event":"file_closed","interface":"tap0",)"));
  EXPECT_TRUE(logged.has(R"("packets":5000,"bytes":580024})"));
  // A replayed file has no counters.
  EXPECT_TRUE(logged.has(R"("event":"counters_unavailable","interface":"tap0")"));
}

TEST(Service, LogsTheFilesItsByteLimitCloses) {
  const TempDir dir;
  // Room for the header and 10 records of 116 bytes.
  Service service = replaying(recorded(dir / "", 25), dir / "", {std::chrono::hours(1), 24 + 10 * 116});
  Logged logged;
  ASSERT_TRUE(service.step(kStart, logged.logger).has_value());
  EXPECT_TRUE(logged.has(R"("packets":10,"bytes":1184})"));
}

TEST(Service, RotatesTheFilesThatAreDue) {
  const TempDir dir;
  Service service = replaying(recorded(dir / "", 1), dir / "", {seconds(1), 1U << 30U});
  Logged logged;
  ASSERT_TRUE(service.step(kStart, logged.logger).has_value());
  EXPECT_FALSE(logged.has(R"("event":"file_closed")"));
  ASSERT_TRUE(service.step(kStart + seconds(1), logged.logger).has_value());
  EXPECT_TRUE(logged.has(R"(tap0-20261003T171500.000000000Z.pcap","sha256":)"));
  EXPECT_TRUE(std::filesystem::exists(dir / "tap0-20261003T171501.000000000Z.pcap"));
}

TEST(Service, StopsAtTheFirstFailure) {
  const TempDir dir;
  Service service = replaying(recorded(dir / "", 1), dir / "", {seconds(1), 1U << 30U});
  std::ofstream(dir / "tap0-20261003T171501.000000000Z.pcap") << "kept";
  Logged logged;
  EXPECT_EQ(service.step(kStart + seconds(1), logged.logger).error(), ics::Error::kUnwritable);
  EXPECT_TRUE(logged.has(R"("event":"capture_failed","interface":"tap0","error":"unwritable"})"));
}

TEST(Service, StopsWhenAPacketCannotBeWritten) {
  const TempDir dir;
  // The second packet needs the next file, whose name is taken.
  Service service = replaying(recorded(dir / "", 2), dir / "", {std::chrono::hours(1), 24 + 116});
  std::ofstream(dir / "tap0-20261003T171500.000000001Z.pcap") << "kept";
  Logged logged;
  EXPECT_EQ(service.step(kStart, logged.logger).error(), ics::Error::kUnwritable);
  EXPECT_TRUE(logged.has(R"("event":"capture_failed","interface":"tap0","error":"unwritable"})"));
}

TEST(Service, ClosesEveryFileAndReturnsTheFirstFailure) {
  const TempDir dir;
  const std::filesystem::path source = recorded(dir / "", 1);
  std::vector<Port> ports;
  ports.push_back(replay("tap0", source, dir / "", kHour));
  ports.push_back(replay("tap1", source, dir / "", kHour));
  Service service(std::move(ports));
  std::ofstream(dir / "tap0-20261003T171500.000000000Z.pcap.sha256") << "kept";
  std::ofstream(dir / "tap1-20261003T171500.000000000Z.pcap.sha256") << "kept";
  Logged logged;
  EXPECT_EQ(service.close(logged.logger).error(), ics::Error::kUnwritable);
  EXPECT_TRUE(logged.has(R"("event":"close_failed","interface":"tap0","error":"unwritable"})"));
  EXPECT_TRUE(logged.has(R"("event":"close_failed","interface":"tap1","error":"unwritable"})"));
}

TEST(Service, CapturesLiveFromTheConfiguredInterfaces) {
  const TempDir dir;
  std::string reason;
  const ics::UtcTime now = ics::logging::system_now();
  Service service = Service::open(ics::capd::testing::loopback_config(dir / ""), now, reason).value();
  ics::capd::testing::send_udp(47921, 200);
  Logged logged;
  for (int i = 0; i < 5; ++i) {
    std::vector<pollfd> ready = service.descriptors();
    static_cast<void>(::poll(ready.data(), ready.size(), 100));
    ASSERT_TRUE(service.step(now, logged.logger).has_value());
  }
  ASSERT_TRUE(service.close(logged.logger).has_value());
  EXPECT_TRUE(logged.has(R"("event":"counters","interface":"lo","received":)"));
  EXPECT_FALSE(logged.has(R"("packets":0,)"));
}

TEST(Service, RefusesBadOrRepeatedInterfaceNames) {
  const TempDir dir;
  ics::capd::Config config = ics::capd::testing::loopback_config(dir / "");
  std::string reason;
  config.interfaces = {"lo", "lo"};
  EXPECT_EQ(Service::open(config, kStart, reason).error(), ics::Error::kInvalidArgument);
  config.interfaces = {"../lo"};
  EXPECT_EQ(Service::open(config, kStart, reason).error(), ics::Error::kInvalidArgument);
  EXPECT_EQ(reason, "interface names must be valid and different");
}

TEST(Service, ExplainsAnInterfaceOrFolderItCannotOpen) {
  const TempDir dir;
  ics::capd::Config config = ics::capd::testing::loopback_config(dir / "missing");
  std::string reason;
  EXPECT_EQ(Service::open(config, kStart, reason).error(), ics::Error::kUnwritable);
  EXPECT_EQ(reason, "cannot create a capture file in " + (dir / "missing").native());
  config.interfaces = {"ics-no-such"};
  EXPECT_EQ(Service::open(config, kStart, reason).error(), ics::Error::kUnavailable);
  EXPECT_NE(reason.find("ics-no-such: "), std::string::npos) << reason;
}

TEST(Service, ReadsTaiMinusUtcFromPtp4lForAdapterTimeStamps) {
  const TempDir dir;
  ics::capd::Config config = ics::capd::testing::loopback_config(dir / "");
  config.timestamps = ics::capture::TimestampSource::kAdapter;
  config.ptp = ics::capd::PtpConfig{dir / "ptp4l-ro", dir / "client", 0};
  std::string reason;
  EXPECT_EQ(Service::open(config, kStart, reason).error(), ics::Error::kUnavailable);
  EXPECT_EQ(reason, "no valid TAI - UTC offset from ptp4l at " + (dir / "ptp4l-ro").native());
  // With ptp4l answering, the loopback interface is what fails: it has no
  // hardware clock.
  const ics::capd::testing::AnsweringPtp4l ptp4l(dir / "ptp4l-ro", dir / "client", ics::capd::testing::kOffset37);
  EXPECT_EQ(Service::open(config, kStart, reason).error(), ics::Error::kUnavailable);
  EXPECT_NE(reason.find("lo: "), std::string::npos) << reason;
}

}  // namespace
