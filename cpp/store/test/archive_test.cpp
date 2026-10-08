#include "ics/store/archive.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "golden_pli.hpp"
#include "ics/common/units.hpp"
#include "ics/store/crc32c.hpp"
#include "ics/store/segment_format.hpp"
#include "ics/store/segment_writer.hpp"
#include "store_support.hpp"
#include "support.hpp"

namespace {

using ics::store::archive_paths;
using ics::store::archive_segment;
using ics::store::ArchiveCounts;
using ics::store::is_archived;
using ics::store::SegmentWriter;
using ics::store::testing::event;
using ics::store::testing::file_bytes;
using ics::store::testing::golden_events;
using ics::store::testing::golden_records;
using ics::store::testing::kStartNs;
using ics::store::testing::record;
using ics::store::testing::write_file;
using ics::timing::testing::TempDir;

const ics::UtcTime kStart = ics::utc_from_ns(kStartNs);

// An entry of kind holding payload, framed as the segment format says.
std::vector<std::byte> raw_entry(const ics::store::EntryKind kind, const std::vector<std::byte>& payload) {
  std::vector<std::byte> body{static_cast<std::byte>(kind)};
  body.insert(body.end(), payload.begin(), payload.end());
  std::vector<std::byte> out;
  const auto put = [&out](const std::uint32_t value) {
    for (unsigned shift = 0; shift < 32U; shift += 8U) {
      out.push_back(static_cast<std::byte>(value >> shift));
    }
  };
  put(static_cast<std::uint32_t>(payload.size()));
  put(ics::store::crc32c(body));
  out.insert(out.end(), body.begin(), body.end());
  return out;
}

// The golden PLI, records and events in turn, as one closed segment.
std::filesystem::path golden_segment(const std::filesystem::path& folder) {
  SegmentWriter writer = SegmentWriter::open(folder, kStart).value();
  const std::vector<ics::v1::PliRecord> records = golden_records();
  const std::vector<ics::v1::PliEvent> events = golden_events();
  for (std::size_t index = 0; index < records.size(); ++index) {
    EXPECT_TRUE(writer.append(records[index]).has_value());
    if (index < events.size()) {
      EXPECT_TRUE(writer.append(events[index]).has_value());
    }
  }
  return writer.close().value();
}

TEST(ArchivePaths, NamesTheParquetFilesForTheSegment) {
  const ics::store::ArchivePaths paths = archive_paths("/store/pli-20261003T171500.123456789Z.icspli");
  EXPECT_EQ(paths.records, "/store/pli-20261003T171500.123456789Z.records.parquet");
  EXPECT_EQ(paths.events, "/store/pli-20261003T171500.123456789Z.events.parquet");
}

TEST(ArchiveSegment, WritesBothFilesAndNothingElse) {
  const TempDir folder;
  const std::filesystem::path segment = golden_segment(folder / "");
  EXPECT_FALSE(is_archived(segment));
  const ArchiveCounts counts = archive_segment(segment, {.rows = 3, .bytes = 1U << 20U}).value();
  EXPECT_EQ(counts.records, golden_records().size());
  EXPECT_EQ(counts.events, golden_events().size());
  EXPECT_TRUE(is_archived(segment));
  std::size_t files = 0;
  for ([[maybe_unused]] const auto& entry : std::filesystem::directory_iterator(folder / "")) {
    ++files;
  }
  EXPECT_EQ(files, 3U);
}

TEST(ArchiveSegment, MatchesTheGoldenFiles) {
  // golden/pli holds the segment and its archive; ICS_WRITE_GOLDEN=1 rewrites
  // them, and python/tests/test_pli_parquet.py reads them with pyarrow.
  const TempDir folder;
  const std::filesystem::path segment = golden_segment(folder / "");
  ASSERT_TRUE(archive_segment(segment, {.rows = 3, .bytes = 1U << 20U}).has_value());
  const std::filesystem::path golden = ICS_PLI_GOLDEN;
  const ics::store::ArchivePaths paths = archive_paths(segment);
  const bool rewrite = std::getenv("ICS_WRITE_GOLDEN") != nullptr;
  for (const std::filesystem::path& path : {segment, paths.records, paths.events}) {
    if (rewrite) {
      std::filesystem::copy_file(path, golden / path.filename(), std::filesystem::copy_options::overwrite_existing);
    }
    EXPECT_EQ(file_bytes(path), file_bytes(golden / path.filename())) << path.filename();
  }
}

TEST(ArchiveSegment, StartsARowGroupWhenOneHoldsEnoughBytes) {
  const TempDir folder;
  const std::filesystem::path segment = golden_segment(folder / "");
  const std::filesystem::path by_rows = archive_paths(segment).records;
  ASSERT_TRUE(archive_segment(segment, {.rows = 1000, .bytes = 1}).has_value());
  const std::vector<std::byte> one_per_row = file_bytes(by_rows);
  ASSERT_TRUE(archive_segment(segment).has_value());
  EXPECT_GT(one_per_row.size(), file_bytes(by_rows).size());
}

TEST(ArchiveSegment, ReplacesAnEarlierArchiveAndAPartialFile) {
  const TempDir folder;
  const std::filesystem::path segment = golden_segment(folder / "");
  const ics::store::ArchivePaths paths = archive_paths(segment);
  std::ofstream(paths.records.string() + ".partial") << "torn";
  std::ofstream(paths.events) << "old";
  ASSERT_TRUE(archive_segment(segment).has_value());
  EXPECT_FALSE(std::filesystem::exists(paths.records.string() + ".partial"));
  EXPECT_GT(std::filesystem::file_size(paths.events), 3U);
}

TEST(ArchiveSegment, ArchivesOnlyTheSoundEntries) {
  const TempDir folder;
  SegmentWriter writer = SegmentWriter::open(folder / "", kStart).value();
  ASSERT_TRUE(writer.append(record(0)).has_value());
  ASSERT_TRUE(writer.append(event(1)).has_value());
  const std::filesystem::path segment = writer.close().value();
  const std::vector<std::byte> whole = file_bytes(segment);
  write_file(segment, {whole.begin(), whole.end() - 1});
  const ArchiveCounts counts = archive_segment(segment).value();
  EXPECT_EQ(counts.records, 1U);
  EXPECT_EQ(counts.events, 0U);
}

TEST(ArchiveSegment, ReportsASegmentItCannotRead) {
  const TempDir folder;
  EXPECT_EQ(archive_segment(folder / "missing.icspli").error(), ics::Error::kUnreadable);
  const std::filesystem::path unreadable = folder / "folder.icspli";
  std::filesystem::create_directory(unreadable);
  EXPECT_EQ(archive_segment(unreadable).error(), ics::Error::kUnreadable);
  const std::filesystem::path text = folder / "text.icspli";
  std::ofstream(text) << "not a segment";
  EXPECT_EQ(archive_segment(text).error(), ics::Error::kMalformed);
  EXPECT_FALSE(is_archived(text));
}

TEST(ArchiveSegment, ReportsAnEntryThatDoesNotHoldItsMessage) {
  const TempDir folder;
  const std::filesystem::path segment = folder / "bad.icspli";
  for (const ics::store::EntryKind kind : {ics::store::EntryKind::kRecord, ics::store::EntryKind::kEvent}) {
    // A sound entry whose payload ends inside a field's tag.
    std::vector<std::byte> bytes(ics::store::kSegmentMagic.begin(), ics::store::kSegmentMagic.end());
    const std::vector<std::byte> entry = raw_entry(kind, {std::byte{0xFF}});
    bytes.insert(bytes.end(), entry.begin(), entry.end());
    write_file(segment, bytes);
    EXPECT_EQ(archive_segment(segment).error(), ics::Error::kMalformed);
  }
}

TEST(ArchiveSegment, ReportsAFileItCannotWriteOrRename) {
  const TempDir folder;
  const std::filesystem::path segment = golden_segment(folder / "");
  const ics::store::ArchivePaths paths = archive_paths(segment);
  for (const std::filesystem::path& blocked :
       {std::filesystem::path(paths.records.string() + ".partial"),
        std::filesystem::path(paths.events.string() + ".partial"), paths.records, paths.events}) {
    // A folder that is not empty can be neither removed nor replaced.
    std::filesystem::create_directories(blocked / "inside");
    EXPECT_EQ(archive_segment(segment).error(), ics::Error::kUnwritable) << blocked;
    std::filesystem::remove_all(blocked);
  }
}

}  // namespace
