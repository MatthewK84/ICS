#include "ics/capture/rotating_writer.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "capture_support.hpp"
#include "ics/capture/pcap_file.hpp"
#include "ics/capture/sha256.hpp"
#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/testing/no_allocation_scope.hpp"
#include "support.hpp"

namespace {

using ics::capture::ClosedFile;
using ics::capture::Packet;
using ics::capture::RotatingWriter;
using ics::capture::WriterSettings;
using ics::capture::testing::FileSizeLimit;
using ics::capture::testing::read_packets;
using ics::timing::testing::TempDir;
using std::chrono::milliseconds;
using std::chrono::seconds;

constexpr std::uint32_t kSnaplen = 64;
constexpr std::size_t kRecord = ics::capture::kRecordHeaderSize + kSnaplen;
const ics::UtcTime kStart = ics::utc_from_ns(1'790'000'000'123'456'789);

WriterSettings settings_in(const TempDir& dir) {
  return {dir / "", "tap0", kSnaplen, ics::capture::kLinkEthernet, {seconds(1), 1'000'000}, 4096};
}

// A packet of size bytes, each its index, at offset after kStart.
struct TestPacket {
  std::vector<std::byte> data;
  Packet packet;
};

TestPacket packet_at(const ics::Duration offset, const std::size_t size) {
  TestPacket out;
  for (std::size_t i = 0; i < size; ++i) {
    out.data.push_back(static_cast<std::byte>(i));
  }
  out.packet = Packet{kStart + offset, static_cast<std::uint32_t>(size + 4), out.data};
  return out;
}

ClosedFile closed_file(ics::Result<std::optional<ClosedFile>> result) {
  EXPECT_TRUE(result.has_value() && result->has_value());
  return result.has_value() && result->has_value() ? std::move(**result) : ClosedFile{};
}

void expect_error(const ics::Result<std::optional<ClosedFile>>& result, const ics::Error error) {
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), error);
}

TEST(RotatingWriter, RejectsSettingsThatCannotWork) {
  const TempDir dir;
  std::vector<WriterSettings> invalid(5, settings_in(dir));
  invalid[0].interface.clear();
  invalid[1].interface = "a/b";
  invalid[2].limits.interval = ics::Duration::zero();
  invalid[3].limits.max_bytes = ics::capture::kFileHeaderSize + kRecord - 1;
  invalid[4].buffer_bytes = kRecord - 1;
  for (const WriterSettings& settings : invalid) {
    const auto writer = RotatingWriter::make(settings);
    ASSERT_FALSE(writer.has_value());
    EXPECT_EQ(writer.error(), ics::Error::kInvalidArgument);
  }
  WriterSettings missing = settings_in(dir);
  missing.directory = dir / "missing";
  ASSERT_FALSE(RotatingWriter::make(missing).has_value());
  EXPECT_EQ(RotatingWriter::make(missing).error(), ics::Error::kUnwritable);
}

TEST(RotatingWriter, WritesAFileLibpcapReadsAndHashesIt) {
  const TempDir dir;
  RotatingWriter writer = RotatingWriter::make(settings_in(dir)).value();
  const TestPacket a = packet_at(milliseconds(0), 10);
  const TestPacket b = packet_at(milliseconds(1), kSnaplen);
  ASSERT_FALSE(writer.write(a.packet).value().has_value());
  ASSERT_FALSE(writer.write(b.packet).value().has_value());
  EXPECT_TRUE(writer.file_open());
  const ClosedFile file = closed_file(writer.close());
  EXPECT_FALSE(writer.file_open());
  EXPECT_EQ(file.path.filename(), "tap0-20260921T141320.123456789Z.pcap");
  EXPECT_EQ(file.packets, 2U);
  EXPECT_EQ(file.bytes, ics::capture::kFileHeaderSize + 2 * ics::capture::kRecordHeaderSize + 10 + kSnaplen);
  EXPECT_EQ(std::filesystem::file_size(file.path), file.bytes);
  const auto packets = read_packets(file.path);
  ASSERT_EQ(packets.size(), 2U);
  EXPECT_EQ(packets[0].utc_ns, 1'790'000'000'123'456'789);
  EXPECT_EQ(packets[1].utc_ns, 1'790'000'000'124'456'789);
  EXPECT_EQ(packets[0].wire_length, 14U);
  EXPECT_EQ(packets[1].data, b.data);
  // The sidecar holds the hash of the file's bytes, in sha256sum's format.
  ics::capture::Sha256 hash = ics::capture::Sha256::make().value();
  hash.update(ics::capture::testing::file_bytes(file.path));
  const auto digest = hash.finish().value();
  EXPECT_EQ(digest, file.sha256);
  const ics::capture::DigestHex hex = ics::capture::to_hex(digest);
  EXPECT_EQ(ics::capture::testing::file_text(dir / "tap0-20260921T141320.123456789Z.pcap.sha256"),
            std::string(hex.begin(), hex.end()) + "  tap0-20260921T141320.123456789Z.pcap\n");
}

TEST(RotatingWriter, RotatesWhenTheIntervalPasses) {
  const TempDir dir;
  RotatingWriter writer = RotatingWriter::make(settings_in(dir)).value();
  ASSERT_FALSE(writer.write(packet_at(milliseconds(0), 10).packet).value().has_value());
  ASSERT_FALSE(writer.write(packet_at(milliseconds(999), 10).packet).value().has_value());
  const ClosedFile first = closed_file(writer.write(packet_at(milliseconds(1000), 10).packet));
  EXPECT_EQ(first.packets, 2U);
  const ClosedFile second = closed_file(writer.close());
  EXPECT_EQ(second.path.filename(), "tap0-20260921T141321.123456789Z.pcap");
  EXPECT_EQ(read_packets(second.path).size(), 1U);
}

TEST(RotatingWriter, RotatesBeforePassingTheSizeLimit) {
  const TempDir dir;
  WriterSettings settings = settings_in(dir);
  settings.limits.max_bytes = ics::capture::kFileHeaderSize + 2 * kRecord;
  RotatingWriter writer = RotatingWriter::make(settings).value();
  ASSERT_FALSE(writer.write(packet_at(milliseconds(0), kSnaplen).packet).value().has_value());
  ASSERT_FALSE(writer.write(packet_at(milliseconds(1), kSnaplen).packet).value().has_value());
  EXPECT_EQ(closed_file(writer.write(packet_at(milliseconds(2), 1).packet)).packets, 2U);
}

TEST(RotatingWriter, ClosesAQuietFileWhenDue) {
  const TempDir dir;
  RotatingWriter writer = RotatingWriter::make(settings_in(dir)).value();
  EXPECT_FALSE(writer.close_if_due(kStart).value().has_value());
  EXPECT_FALSE(writer.close().value().has_value());
  ASSERT_FALSE(writer.write(packet_at(milliseconds(0), 10).packet).value().has_value());
  EXPECT_FALSE(writer.close_if_due(kStart + milliseconds(999)).value().has_value());
  EXPECT_EQ(closed_file(writer.close_if_due(kStart + seconds(1))).packets, 1U);
}

TEST(RotatingWriter, FlushesAFullBuffer) {
  const TempDir dir;
  WriterSettings settings = settings_in(dir);
  settings.buffer_bytes = kRecord;
  RotatingWriter writer = RotatingWriter::make(settings).value();
  for (int i = 0; i < 20; ++i) {
    ASSERT_FALSE(writer.write(packet_at(milliseconds(i), kSnaplen).packet).value().has_value());
  }
  EXPECT_EQ(read_packets(closed_file(writer.close()).path).size(), 20U);
}

TEST(RotatingWriter, WritesWithoutAllocating) {
  const TempDir dir;
  RotatingWriter writer = RotatingWriter::make(settings_in(dir)).value();
  const TestPacket first = packet_at(milliseconds(0), 10);
  const TestPacket next = packet_at(milliseconds(1), 10);
  ASSERT_TRUE(writer.write(first.packet).has_value());
  const ics::testing::NoAllocationScope no_allocation;
  EXPECT_TRUE(writer.write(next.packet).has_value());
}

TEST(RotatingWriter, RejectsAPacketPastTheSnapshotLength) {
  const TempDir dir;
  RotatingWriter writer = RotatingWriter::make(settings_in(dir)).value();
  expect_error(writer.write(packet_at(milliseconds(0), kSnaplen + 1).packet), ics::Error::kInvalidArgument);
}

TEST(RotatingWriter, NeverOverwritesAFile) {
  const TempDir dir;
  std::ofstream(dir / "tap0-20260921T141320.123456789Z.pcap") << "kept";
  RotatingWriter writer = RotatingWriter::make(settings_in(dir)).value();
  expect_error(writer.write(packet_at(milliseconds(0), 10).packet), ics::Error::kUnwritable);
  EXPECT_EQ(ics::capture::testing::file_text(dir / "tap0-20260921T141320.123456789Z.pcap"), "kept");
}

TEST(RotatingWriter, ReportsAFailedWrite) {
  const TempDir dir;
  WriterSettings settings = settings_in(dir);
  settings.buffer_bytes = kRecord;
  RotatingWriter writer = RotatingWriter::make(settings).value();
  // The first packet flushes the 24-byte header. The second flushes the
  // first record, 80 bytes, into a file with room for 26 more: one short
  // write, then a failed one.
  const FileSizeLimit limit(50);
  ASSERT_TRUE(writer.write(packet_at(milliseconds(0), kSnaplen).packet).has_value());
  expect_error(writer.write(packet_at(milliseconds(1), kSnaplen).packet), ics::Error::kUnwritable);
}

TEST(RotatingWriter, ReportsAFailedClose) {
  const TempDir dir;
  RotatingWriter writer = RotatingWriter::make(settings_in(dir)).value();
  const FileSizeLimit limit(10);
  ASSERT_TRUE(writer.write(packet_at(milliseconds(0), 10).packet).has_value());
  // Rotating closes the file, whose bytes do not fit.
  expect_error(writer.write(packet_at(seconds(1), 10).packet), ics::Error::kUnwritable);
}

TEST(RotatingWriter, ReportsASidecarItCannotCreate) {
  const TempDir dir;
  std::ofstream(dir / "tap0-20260921T141320.123456789Z.pcap.sha256") << "kept";
  RotatingWriter writer = RotatingWriter::make(settings_in(dir)).value();
  ASSERT_TRUE(writer.write(packet_at(milliseconds(0), 10).packet).has_value());
  expect_error(writer.close(), ics::Error::kUnwritable);
}

}  // namespace
