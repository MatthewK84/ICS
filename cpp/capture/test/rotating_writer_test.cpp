#include "ics/capture/rotating_writer.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "capture_support.hpp"
#include "ics/capture/pcap_format.hpp"
#include "ics/testing/no_allocation_scope.hpp"
#include "support.hpp"

namespace {

using ics::capture::ClosedFile;
using ics::capture::FileFormat;
using ics::capture::kFileHeaderSize;
using ics::capture::kRecordHeaderSize;
using ics::capture::Packet;
using ics::capture::RotatingWriter;
using ics::capture::RotationLimits;
using ics::capture::testing::read_bytes;
using ics::capture::testing::read_text;
using ics::capture::testing::sha256_hex;
using ics::timing::testing::TempDir;
using std::chrono::seconds;

constexpr FileFormat kEthernet{65535, 1};
constexpr RotationLimits kHourOrGigabyte{std::chrono::hours(1), 1'000'000'000};
// 2026-10-03T17:15:00Z.
const ics::UtcTime kStart = ics::utc_from_ns(1'791'047'700'000'000'000);

Packet packet_of(const std::vector<std::byte>& bytes, const ics::UtcTime time) {
  return Packet{time, static_cast<std::uint32_t>(bytes.size() + 4), bytes};
}

std::uint64_t file_bytes(const std::size_t packets, const std::size_t packet_bytes) {
  return kFileHeaderSize + packets * (kRecordHeaderSize + packet_bytes);
}

// Expects closed to describe a file of its packets and bytes whose .sha256
// file holds its hash, as sha256sum -c checks it.
void expect_hashed(const ClosedFile& closed, const std::uint64_t packets, const std::uint64_t bytes) {
  EXPECT_EQ(closed.packets, packets);
  EXPECT_EQ(closed.bytes, bytes);
  const std::vector<std::byte> written = read_bytes(closed.path);
  EXPECT_EQ(written.size(), bytes);
  EXPECT_EQ(closed.sha256, sha256_hex(written));
  EXPECT_EQ(read_text(closed.path.native() + ".sha256"), closed.sha256 + "  " + closed.path.filename().native() + "\n");
}

TEST(RotatingWriter, NamesFilesByTheUtcTimeTheyOpened) {
  EXPECT_EQ(ics::capture::file_name("tap0", ics::utc_from_ns(1'791'047'700'123'456'789)),
            "tap0-20261003T171500.123456789Z.pcap");
  EXPECT_EQ(ics::capture::file_name("tap1", kStart), "tap1-20261003T171500.000000000Z.pcap");
}

TEST(RotatingWriter, WritesPacketsAndHashesTheFileWhenItCloses) {
  const TempDir folder;
  RotatingWriter writer = RotatingWriter::open(folder / "", "tap0", kEthernet, kHourOrGigabyte, kStart).value();
  EXPECT_EQ(writer.path(), folder / "tap0-20261003T171500.000000000Z.pcap");
  const std::vector<std::byte> bytes(60, std::byte{0x5A});
  for (int i = 0; i < 3; ++i) {
    const auto closed = writer.write(packet_of(bytes, kStart + seconds(i)), kStart + seconds(i));
    ASSERT_TRUE(closed.has_value());
    EXPECT_FALSE(closed->has_value());
  }
  expect_hashed(writer.close().value(), 3, file_bytes(3, 60));
}

TEST(RotatingWriter, HashesFilesLargerThanItsBuffer) {
  const TempDir folder;
  RotatingWriter writer = RotatingWriter::open(folder / "", "tap0", kEthernet, kHourOrGigabyte, kStart).value();
  const std::vector<std::byte> bytes(1500, std::byte{0x11});
  constexpr std::size_t kPackets = 2000;  // About 3 MB, three buffers' worth.
  for (std::size_t i = 0; i < kPackets; ++i) {
    ASSERT_TRUE(writer.write(packet_of(bytes, kStart), kStart).has_value());
  }
  expect_hashed(writer.close().value(), kPackets, file_bytes(kPackets, 1500));
}

TEST(RotatingWriter, AllocatesNothingPerPacket) {
  const TempDir folder;
  RotatingWriter writer = RotatingWriter::open(folder / "", "tap0", kEthernet, kHourOrGigabyte, kStart).value();
  const std::vector<std::byte> bytes(1500, std::byte{0x22});
  constexpr std::size_t kPackets = 1000;  // Over 1 MB, so the buffer is hashed and written too.
  {
    const ics::testing::NoAllocationScope no_allocation;
    for (std::size_t i = 0; i < kPackets; ++i) {
      static_cast<void>(writer.write(packet_of(bytes, kStart), kStart));
    }
  }
  expect_hashed(writer.close().value(), kPackets, file_bytes(kPackets, 1500));
}

TEST(RotatingWriter, StartsTheNextFileBeforeARecordWouldPassTheByteLimit) {
  const TempDir folder;
  const std::vector<std::byte> bytes(100, std::byte{1});
  // Room for the header and two records; a single packet larger than the
  // limit still goes in an empty file.
  const RotationLimits limits{std::chrono::hours(1), file_bytes(2, 100)};
  RotatingWriter writer = RotatingWriter::open(folder / "", "tap0", kEthernet, limits, kStart).value();
  ASSERT_TRUE(writer.write(packet_of(bytes, kStart), kStart).value() == std::nullopt);
  ASSERT_TRUE(writer.write(packet_of(bytes, kStart), kStart).value() == std::nullopt);
  const std::optional<ClosedFile> closed = writer.write(packet_of(bytes, kStart), kStart + seconds(1)).value();
  ASSERT_TRUE(closed.has_value());
  expect_hashed(*closed, 2, file_bytes(2, 100));
  EXPECT_EQ(writer.path(), folder / "tap0-20261003T171501.000000000Z.pcap");

  const std::vector<std::byte> large(limits.max_bytes, std::byte{2});
  const std::optional<ClosedFile> second = writer.write(packet_of(large, kStart), kStart + seconds(2)).value();
  ASSERT_TRUE(second.has_value());
  expect_hashed(*second, 1, file_bytes(1, 100));
  expect_hashed(writer.close().value(), 1, kFileHeaderSize + kRecordHeaderSize + large.size());
}

TEST(RotatingWriter, NamesFilesOpenedInTheSameNanosecondApart) {
  const TempDir folder;
  const std::vector<std::byte> bytes(100, std::byte{1});
  const RotationLimits limits{std::chrono::hours(1), file_bytes(1, 100)};
  RotatingWriter writer = RotatingWriter::open(folder / "", "tap0", kEthernet, limits, kStart).value();
  for (int i = 0; i < 3; ++i) {
    ASSERT_TRUE(writer.write(packet_of(bytes, kStart), kStart).has_value());
  }
  EXPECT_EQ(writer.path(), folder / "tap0-20261003T171500.000000002Z.pcap");
}

TEST(RotatingWriter, RotatesWhenAFileReachesItsAge) {
  const TempDir folder;
  RotatingWriter writer = RotatingWriter::open(folder / "", "tap0", kEthernet, kHourOrGigabyte, kStart).value();
  EXPECT_FALSE(writer.due(kStart + std::chrono::minutes(59)));
  EXPECT_TRUE(writer.due(kStart + std::chrono::hours(1)));
  // A file with no packets is still closed and hashed: it shows the capture ran.
  expect_hashed(writer.rotate(kStart + std::chrono::hours(1)).value(), 0, kFileHeaderSize);
  EXPECT_EQ(writer.path(), folder / "tap0-20261003T181500.000000000Z.pcap");
  EXPECT_FALSE(writer.due(kStart + std::chrono::hours(1)));
  expect_hashed(writer.close().value(), 0, kFileHeaderSize);
}

TEST(RotatingWriter, RefusesToWriteOrRotateOnceClosed) {
  const TempDir folder;
  RotatingWriter writer = RotatingWriter::open(folder / "", "tap0", kEthernet, kHourOrGigabyte, kStart).value();
  ASSERT_TRUE(writer.close().has_value());
  const std::vector<std::byte> bytes(10, std::byte{1});
  EXPECT_EQ(writer.write(packet_of(bytes, kStart), kStart).error(), ics::Error::kInvalidArgument);
  EXPECT_EQ(writer.rotate(kStart).error(), ics::Error::kInvalidArgument);
  EXPECT_EQ(writer.close().error(), ics::Error::kInvalidArgument);
}

TEST(RotatingWriter, RefusesAPacketTimeAPcapFileCannotHold) {
  const TempDir folder;
  RotatingWriter writer = RotatingWriter::open(folder / "", "tap0", kEthernet, kHourOrGigabyte, kStart).value();
  const std::vector<std::byte> bytes(10, std::byte{1});
  EXPECT_EQ(writer.write(packet_of(bytes, ics::utc_from_ns(-1)), kStart).error(), ics::Error::kOutOfRange);
}

TEST(RotatingWriter, FailsWhenAFileCannotBeCreated) {
  const TempDir folder;
  EXPECT_EQ(RotatingWriter::open(folder / "missing", "tap0", kEthernet, kHourOrGigabyte, kStart).error(),
            ics::Error::kUnwritable);
  RotatingWriter writer = RotatingWriter::open(folder / "", "tap0", kEthernet, kHourOrGigabyte, kStart).value();
  // Something is already where the next file would go.
  std::ofstream(folder / "tap0-20261003T171501.000000000Z.pcap") << "kept";
  EXPECT_EQ(writer.rotate(kStart + seconds(1)).error(), ics::Error::kUnwritable);
}

TEST(RotatingWriter, FailsAByteLimitRotationWhoseNextFileCannotBeCreated) {
  const TempDir folder;
  const std::vector<std::byte> bytes(100, std::byte{1});
  const RotationLimits limits{std::chrono::hours(1), file_bytes(1, 100)};
  RotatingWriter writer = RotatingWriter::open(folder / "", "tap0", kEthernet, limits, kStart).value();
  ASSERT_TRUE(writer.write(packet_of(bytes, kStart), kStart).has_value());
  std::ofstream(folder / "tap0-20261003T171501.000000000Z.pcap") << "kept";
  EXPECT_EQ(writer.write(packet_of(bytes, kStart), kStart + seconds(1)).error(), ics::Error::kUnwritable);
}

TEST(RotatingWriter, FailsWhenTheHashFileCannotBeCreated) {
  const TempDir folder;
  RotatingWriter writer = RotatingWriter::open(folder / "", "tap0", kEthernet, kHourOrGigabyte, kStart).value();
  std::ofstream(writer.path().native() + ".sha256") << "kept";
  EXPECT_EQ(writer.rotate(kStart + seconds(1)).error(), ics::Error::kUnwritable);
  // The failed file is ended either way.
  EXPECT_EQ(writer.close().error(), ics::Error::kInvalidArgument);
}

TEST(RotatingWriter, FailsWhenTheDiskRefusesBytes) {
  const TempDir folder;
  RotatingWriter writer = RotatingWriter::open(folder / "", "tap0", kEthernet, kHourOrGigabyte, kStart).value();
  const std::vector<std::byte> bytes(1500, std::byte{1});
  const ics::capture::testing::FileSizeLimit limit(1000);
  ics::Status written;
  for (int i = 0; i < 1000 && written; ++i) {
    written = writer.write(packet_of(bytes, kStart), kStart).map([](const auto&) {});
  }
  EXPECT_EQ(written.error(), ics::Error::kUnwritable);
}

}  // namespace
