#include "ics/capture/live_capture.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>

#include "capture_support.hpp"
#include "ics/capture/rotating_writer.hpp"
#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/timing/unix_socket.hpp"
#include "support.hpp"

namespace {

using ics::capture::CaptureSettings;
using ics::capture::ClosedFile;
using ics::capture::LiveCapture;
using ics::capture::RotatingWriter;
using ics::capture::TimestampSource;
using ics::capture::WriterSettings;
using ics::capture::testing::marked;
using ics::capture::testing::Loopback;
using ics::timing::testing::TempDir;
using std::chrono::seconds;

constexpr std::uint32_t kSnaplen = 256;
constexpr std::uint32_t kRingBytes = 1U << 20U;
CaptureSettings loopback_settings(const TimestampSource timestamps) { return {"lo", kSnaplen, kRingBytes, timestamps}; }

// Dispatches until at least count packets were read or 3 s pass.
std::size_t read_at_least(LiveCapture& capture, const std::size_t count, const ics::Duration shift,
                          RotatingWriter& writer, std::vector<ClosedFile>& closed) {
  std::size_t read = 0;
  const auto deadline = std::chrono::steady_clock::now() + seconds(3);
  while (read < count && std::chrono::steady_clock::now() < deadline) {
    pollfd ready{capture.fd(), POLLIN, 0};
    ::poll(&ready, 1, 100);
    read += capture.dispatch(64, shift, writer, closed).value_or(0);
  }
  return read;
}

TEST(LiveCapture, CapturesLoopbackTrafficIntoRotatingFiles) {
  const TempDir dir;
  LiveCapture capture = LiveCapture::open(loopback_settings(TimestampSource::kHost)).value();
  EXPECT_EQ(capture.link_type(), ics::capture::kLinkEthernet);
  // Room for about three of the test's datagrams per file, so files rotate
  // while dispatching.
  const WriterSettings settings{dir / "", "lo", kSnaplen, capture.link_type(), {seconds(60), 400}, 4096};
  RotatingWriter writer = RotatingWriter::make(settings).value();
  const Loopback loopback;
  const auto sent_at = std::chrono::system_clock::now();
  loopback.send(10);
  // TAI-UTC, as a NIC clock that keeps TAI needs.
  const ics::Duration shift = seconds(37);
  std::vector<ClosedFile> closed;
  EXPECT_GE(read_at_least(capture, 10, shift, writer, closed), 10U);
  closed.push_back(writer.close().value().value());
  std::vector<std::filesystem::path> files;
  std::ranges::transform(closed, std::back_inserter(files), &ClosedFile::path);
  EXPECT_GT(files.size(), 1U);
  EXPECT_EQ(marked(files), 10U);
  const auto first = ics::capture::testing::read_packets(files.front()).front();
  const ics::UtcTime expected = std::chrono::time_point_cast<ics::Duration>(sent_at) - shift;
  EXPECT_LT(std::chrono::abs(ics::utc_from_ns(first.utc_ns) - expected), seconds(1));
  const ics::capture::CaptureStats stats = capture.stats();
  EXPECT_GE(stats.received, 10U);
  EXPECT_EQ(stats.dropped, 0U);
}

TEST(LiveCapture, RefusesHardwareTimeStampsOnLoopback) {
  const auto capture = LiveCapture::open(loopback_settings(TimestampSource::kAdapter));
  ASSERT_FALSE(capture.has_value());
  EXPECT_EQ(capture.error(), ics::Error::kInvalidArgument);
}

TEST(LiveCapture, ReportsAnInterfaceThatDoesNotExist) {
  CaptureSettings settings = loopback_settings(TimestampSource::kHost);
  settings.interface = "ics-none0";
  const auto capture = LiveCapture::open(settings);
  ASSERT_FALSE(capture.has_value());
  EXPECT_EQ(capture.error(), ics::Error::kUnavailable);
}

TEST(LiveCapture, ReportsNoDescriptorToSpare) {
  const ics::timing::testing::DescriptorLimit limit;
  const auto capture = LiveCapture::open(loopback_settings(TimestampSource::kHost));
  ASSERT_FALSE(capture.has_value());
  EXPECT_EQ(capture.error(), ics::Error::kUnavailable);
}

TEST(LiveCapture, StopsAtAWriterError) {
  const TempDir dir;
  std::filesystem::create_directory(dir / "gone");
  LiveCapture capture = LiveCapture::open(loopback_settings(TimestampSource::kHost)).value();
  RotatingWriter writer =
      RotatingWriter::make({dir / "gone", "lo", kSnaplen, capture.link_type(), {seconds(60), 1'000'000}, 4096})
          .value();
  std::filesystem::remove(dir / "gone");
  const Loopback loopback;
  loopback.send(3);
  std::vector<ClosedFile> closed;
  std::optional<ics::Error> error;
  const auto deadline = std::chrono::steady_clock::now() + seconds(3);
  while (!error.has_value() && std::chrono::steady_clock::now() < deadline) {
    pollfd ready{capture.fd(), POLLIN, 0};
    ::poll(&ready, 1, 100);
    const auto read = capture.dispatch(64, ics::Duration::zero(), writer, closed);
    error = read.has_value() ? std::nullopt : std::optional(read.error());
  }
  EXPECT_EQ(error, ics::Error::kUnwritable);
}

}  // namespace
