#include "ics/capture/capture.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include <arpa/inet.h>
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>

#include "capture_support.hpp"
#include "ics/capture/rotating_writer.hpp"
#include "support.hpp"

namespace {

using ics::capture::Capture;
using ics::capture::CaptureOptions;
using ics::capture::Packet;
using ics::capture::RotatingWriter;
using ics::capture::TimestampSource;
using ics::capture::testing::Collector;
using ics::capture::testing::read_bytes;
using ics::timing::testing::TempDir;

const ics::UtcTime kStart = ics::utc_from_ns(1'791'047'700'000'000'000);

// Writes a pcap file of count packets, each 1 ns and one byte longer than the
// last, and returns its path.
std::filesystem::path write_file(const TempDir& folder, const int count) {
  RotatingWriter writer =
      RotatingWriter::open(folder / "", "tap0", {65535, 1}, {std::chrono::hours(1), 1'000'000}, kStart).value();
  for (int i = 0; i < count; ++i) {
    const std::vector<std::byte> bytes(static_cast<std::size_t>(20 + i), static_cast<std::byte>(i));
    const Packet packet{kStart + std::chrono::nanoseconds(i), static_cast<std::uint32_t>(100 + i), bytes};
    EXPECT_TRUE(writer.write(packet, kStart).has_value());
  }
  return writer.close().value().path;
}

TEST(Capture, ReplaysAFileToTheNanosecond) {
  const TempDir folder;
  std::string reason;
  Capture capture = Capture::open_file(write_file(folder, 3), reason).value();
  EXPECT_GE(capture.fd(), 0);
  EXPECT_EQ(capture.format().snaplen, 65535U);
  EXPECT_EQ(capture.format().linktype, 1U);
  Collector sink;
  EXPECT_EQ(capture.dispatch(sink, 2).value(), 2U);
  EXPECT_EQ(capture.dispatch(sink, 2).value(), 1U);
  EXPECT_EQ(capture.dispatch(sink, 2).value(), 0U);
  ASSERT_EQ(sink.kept.size(), 3U);
  for (std::size_t i = 0; i < sink.kept.size(); ++i) {
    EXPECT_EQ(sink.kept[i].time, kStart + std::chrono::nanoseconds(i));
    EXPECT_EQ(sink.kept[i].original_length, 100 + i);
    EXPECT_EQ(sink.kept[i].bytes, std::vector<std::byte>(20 + i, static_cast<std::byte>(i)));
  }
  EXPECT_EQ(capture.counters().error(), ics::Error::kUnavailable);
}

TEST(Capture, ExplainsAFileItCannotOpen) {
  const TempDir folder;
  std::string reason;
  EXPECT_EQ(Capture::open_file(folder / "missing.pcap", reason).error(), ics::Error::kUnreadable);
  EXPECT_NE(reason.find("missing.pcap"), std::string::npos) << reason;
}

TEST(Capture, StopsWithTheErrorOfASinkThatRefusesAPacket) {
  const TempDir folder;
  std::string reason;
  Capture capture = Capture::open_file(write_file(folder, 3), reason).value();
  Collector sink(ics::fail(ics::Error::kUnwritable));
  EXPECT_EQ(capture.dispatch(sink, 10).error(), ics::Error::kUnwritable);
  EXPECT_EQ(sink.kept.size(), 1U);
}

TEST(Capture, FailsOnATruncatedFile) {
  const TempDir folder;
  const std::vector<std::byte> whole = read_bytes(write_file(folder, 1));
  const std::filesystem::path cut = folder / "cut.pcap";
  std::ofstream(cut, std::ios::binary).write(reinterpret_cast<const char*>(whole.data()),
                                             static_cast<std::streamsize>(whole.size() - 5));
  std::string reason;
  Capture capture = Capture::open_file(cut, reason).value();
  Collector sink;
  EXPECT_EQ(capture.dispatch(sink, 10).error(), ics::Error::kUnreadable);
}

// Sends one UDP datagram of size bytes to port on the loopback interface.
void send_udp(const std::uint16_t port, const std::size_t size) {
  const ics::timing::Fd socket(::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0));
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  const std::vector<std::byte> payload(size, std::byte{0x42});
  ASSERT_EQ(::sendto(socket.get(), payload.data(), payload.size(), 0, reinterpret_cast<const sockaddr*>(&address),
                     sizeof(address)),
            static_cast<ssize_t>(size));
}

// Live capture needs CAP_NET_RAW, which tests have as root in the ics-cpp
// container.
TEST(Capture, CapturesLiveFromAnInterfaceWithHostTimeStamps) {
  constexpr std::uint16_t kPort = 47920;
  constexpr std::size_t kPayload = 333;
  const CaptureOptions options{"lo", 65535, 1U << 20U, TimestampSource::kHost, std::chrono::seconds(37)};
  std::string reason;
  ics::Result<Capture> capture = Capture::open_live(options, reason);
  ASSERT_TRUE(capture.has_value()) << reason;
  Collector sink;
  EXPECT_EQ(capture->dispatch(sink, 10).value(), 0U);  // Nothing waits, and nothing blocks.
  const auto before = std::chrono::system_clock::now();
  send_udp(kPort, kPayload);
  for (int tries = 0; tries < 50 && sink.kept.empty(); ++tries) {
    pollfd ready{capture->fd(), POLLIN, 0};
    static_cast<void>(::poll(&ready, 1, 100));
    ASSERT_TRUE(capture->dispatch(sink, 10).has_value());
  }
  ASSERT_FALSE(sink.kept.empty());
  // The first packet is the datagram: Ethernet, IPv4 and UDP headers, then
  // the payload. Its time is shifted by stamp_minus_utc.
  const Collector::Kept& kept = sink.kept.front();
  EXPECT_EQ(kept.original_length, 14 + 20 + 8 + kPayload);
  EXPECT_EQ(kept.bytes.size(), kept.original_length);
  EXPECT_GE(kept.time + std::chrono::seconds(37), before);
  const ics::capture::Counters counters = capture->counters().value();
  EXPECT_GE(counters.received, 1U);
  EXPECT_EQ(counters.dropped, 0U);
}

TEST(Capture, RefusesAdapterTimeStampsOnAnInterfaceWithoutThem) {
  const CaptureOptions options{"lo", 65535, 1U << 20U, TimestampSource::kAdapter, {}};
  std::string reason;
  EXPECT_EQ(Capture::open_live(options, reason).error(), ics::Error::kUnavailable);
  EXPECT_NE(reason.find("lo: "), std::string::npos) << reason;
}

TEST(Capture, ExplainsAnInterfaceThatDoesNotExist) {
  const CaptureOptions options{"ics-no-such", 65535, 1U << 20U, TimestampSource::kHost, {}};
  std::string reason;
  EXPECT_EQ(Capture::open_live(options, reason).error(), ics::Error::kUnavailable);
  EXPECT_NE(reason.find("ics-no-such: "), std::string::npos) << reason;
}

}  // namespace
