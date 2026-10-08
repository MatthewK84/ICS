#include "ics/store/segment_reader.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "ics/common/units.hpp"
#include "ics/store/segment_format.hpp"
#include "ics/store/segment_writer.hpp"
#include "store_support.hpp"
#include "support.hpp"

namespace {

using ics::store::Entry;
using ics::store::EntryKind;
using ics::store::kSegmentMagic;
using ics::store::SegmentReader;
using ics::store::SegmentWriter;
using ics::store::testing::event;
using ics::store::testing::file_bytes;
using ics::store::testing::kStartNs;
using ics::store::testing::record;
using ics::store::testing::write_file;
using ics::timing::testing::TempDir;

const ics::UtcTime kStart = ics::utc_from_ns(kStartNs);

// A segment of count entries, records and events in turn; returns its path.
std::filesystem::path written(const TempDir& folder, const int count) {
  SegmentWriter writer = SegmentWriter::open(folder / "", kStart).value();
  for (int index = 0; index < count; ++index) {
    const ics::Status appended = index % 2 == 0 ? writer.append(record(index)) : writer.append(event(index));
    EXPECT_TRUE(appended.has_value());
  }
  return writer.close().value();
}

// The kinds and times of every entry the reader returns.
std::vector<std::int64_t> read_times(SegmentReader& reader) {
  std::vector<std::int64_t> times;
  for (std::optional<Entry> entry = reader.next().value(); entry; entry = reader.next().value()) {
    if (entry->kind == EntryKind::kRecord) {
      ics::v1::PliRecord parsed;
      EXPECT_TRUE(parsed.ParseFromArray(entry->payload.data(), static_cast<int>(entry->payload.size())));
      times.push_back(parsed.valid_utc_ns() - kStartNs);
    } else {
      ics::v1::PliEvent parsed;
      EXPECT_TRUE(parsed.ParseFromArray(entry->payload.data(), static_cast<int>(entry->payload.size())));
      times.push_back(parsed.time_utc_ns() - kStartNs);
    }
  }
  return times;
}

std::vector<std::int64_t> expected_times(const int count) {
  constexpr std::int64_t kNsPerMs = 1'000'000;
  std::vector<std::int64_t> out;
  for (int index = 0; index < count; ++index) {
    out.push_back(index * kNsPerMs);
  }
  return out;
}

TEST(SegmentReader, ReadsEveryEntryInOrder) {
  const TempDir folder;
  const std::filesystem::path path = written(folder, 5);
  SegmentReader reader = SegmentReader::open(path).value();
  EXPECT_EQ(read_times(reader), expected_times(5));
  EXPECT_EQ(reader.sound_bytes(), std::filesystem::file_size(path));
  EXPECT_FALSE(reader.stopped());
}

TEST(SegmentReader, ReadsASegmentLargerThanItsBuffer) {
  // About 3 MiB of entries: the reader refills its buffer many times, and
  // the writer writes its buffer before sync.
  const TempDir folder;
  constexpr int kCount = 40'000;
  const std::filesystem::path path = written(folder, kCount);
  ASSERT_GT(std::filesystem::file_size(path), 2 * (ics::store::kMaxEntryBytes + SegmentReader::kReadBytes));
  SegmentReader reader = SegmentReader::open(path).value();
  EXPECT_EQ(read_times(reader), expected_times(kCount));
  EXPECT_EQ(reader.skip_to_end().value(), std::filesystem::file_size(path));
}

TEST(SegmentReader, FollowsASegmentAsItIsWritten) {
  const TempDir folder;
  SegmentWriter writer = SegmentWriter::open(folder / "", kStart).value();
  SegmentReader reader = SegmentReader::open(writer.path()).value();
  EXPECT_FALSE(reader.next().value().has_value());
  ASSERT_TRUE(writer.append(record(0)).has_value());
  ASSERT_TRUE(writer.sync().has_value());
  EXPECT_EQ(read_times(reader), expected_times(1));
  ASSERT_TRUE(writer.append(event(1)).has_value());
  ASSERT_TRUE(writer.sync().has_value());
  EXPECT_EQ(read_times(reader), std::vector<std::int64_t>{1'000'000});
  EXPECT_FALSE(reader.stopped());
}

TEST(SegmentReader, StopsAtATornTail) {
  const TempDir folder;
  const std::filesystem::path path = written(folder, 3);
  const std::vector<std::byte> whole = file_bytes(path);
  SegmentReader two = SegmentReader::open(path).value();
  static_cast<void>(two.next().value());
  static_cast<void>(two.next().value());
  const std::size_t sound = two.sound_bytes();
  for (std::size_t size = sound; size < whole.size(); ++size) {
    write_file(path, {whole.begin(), whole.begin() + static_cast<std::ptrdiff_t>(size)});
    SegmentReader reader = SegmentReader::open(path).value();
    EXPECT_EQ(read_times(reader), expected_times(2)) << size;
    EXPECT_EQ(reader.sound_bytes(), sound);
    EXPECT_FALSE(reader.stopped());
    EXPECT_EQ(ics::store::cut_torn_tail(path).value(), sound);
    EXPECT_EQ(std::filesystem::file_size(path), sound);
  }
}

TEST(SegmentReader, StopsForGoodAtACorruptEntry) {
  const TempDir folder;
  const std::filesystem::path path = written(folder, 3);
  std::vector<std::byte> bytes = file_bytes(path);
  bytes[kSegmentMagic.size() + 4] ^= std::byte{1};  // The first entry's CRC.
  write_file(path, bytes);
  SegmentReader reader = SegmentReader::open(path).value();
  EXPECT_TRUE(read_times(reader).empty());
  EXPECT_TRUE(reader.stopped());
  EXPECT_EQ(reader.sound_bytes(), kSegmentMagic.size());
  EXPECT_FALSE(reader.next().value().has_value());
  EXPECT_EQ(ics::store::cut_torn_tail(path).value(), kSegmentMagic.size());
}

TEST(SegmentReader, TakesATornMagicAsNoEntries) {
  const TempDir folder;
  for (std::size_t size = 0; size < kSegmentMagic.size(); ++size) {
    const std::filesystem::path path = folder / "torn.icspli";
    write_file(path, {kSegmentMagic.begin(), kSegmentMagic.begin() + static_cast<std::ptrdiff_t>(size)});
    SegmentReader reader = SegmentReader::open(path).value();
    EXPECT_FALSE(reader.next().value().has_value());
    EXPECT_TRUE(reader.stopped());
    EXPECT_EQ(reader.sound_bytes(), 0U);
    EXPECT_EQ(ics::store::cut_torn_tail(path).value(), 0U);
    EXPECT_EQ(std::filesystem::file_size(path), 0U);
  }
}

TEST(SegmentReader, RejectsAFileThatIsNotASegment) {
  const TempDir folder;
  const std::filesystem::path path = folder / "notes.txt";
  write_file(path, {std::byte{'n'}, std::byte{'o'}, std::byte{'t'}, std::byte{'e'}});
  SegmentReader reader = SegmentReader::open(path).value();
  EXPECT_EQ(reader.next().error(), ics::Error::kMalformed);
  EXPECT_TRUE(reader.stopped());
  EXPECT_FALSE(reader.next().value().has_value());
  EXPECT_EQ(ics::store::cut_torn_tail(path).error(), ics::Error::kMalformed);
  EXPECT_EQ(std::filesystem::file_size(path), 4U);
}

TEST(SegmentReader, ReportsAFileItCannotOpenOrRead) {
  const TempDir folder;
  EXPECT_EQ(SegmentReader::open(folder / "missing.icspli").error(), ics::Error::kUnreadable);
  // A folder opens, but reading it fails.
  SegmentReader reader = SegmentReader::open(folder / "").value();
  EXPECT_EQ(reader.next().error(), ics::Error::kUnreadable);
  EXPECT_EQ(ics::store::cut_torn_tail(folder / "").error(), ics::Error::kUnreadable);
}

TEST(SegmentReader, ReportsAFileItCannotCut) {
  // /dev/null reads as a torn magic, opens for writing, and cannot be cut.
  EXPECT_EQ(ics::store::cut_torn_tail("/dev/null").error(), ics::Error::kUnwritable);
}

}  // namespace
