#include "ics/store/parquet_writer.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include "capture_support.hpp"
#include "golden_pli.hpp"
#include "ics/v1/pli.pb.h"
#include "ics/v1/pli_query.pb.h"
#include "store_support.hpp"
#include "support.hpp"

namespace {

using ics::store::ParquetWriter;
using ics::store::testing::file_bytes;
using ics::store::testing::golden_events;
using ics::store::testing::golden_records;
using ics::timing::testing::TempDir;

constexpr std::size_t kMagicBytes = 4;
const std::vector<std::byte> kMagic{std::byte{'P'}, std::byte{'A'}, std::byte{'R'}, std::byte{'1'}};

std::uint32_t footer_length(const std::vector<std::byte>& file) {
  const std::span<const std::byte> length = std::span(file).last(kMagicBytes + 4).first(4);
  std::uint32_t value = 0;
  for (std::size_t index = 0; index < length.size(); ++index) {
    value |= std::to_integer<std::uint32_t>(length[index]) << (8U * index);
  }
  return value;
}

TEST(ParquetWriter, FramesTheFileWithTheMagicAndFooter) {
  const TempDir folder;
  ParquetWriter writer = ParquetWriter::create(folder / "records.parquet", *ics::v1::PliRecord::descriptor()).value();
  EXPECT_EQ(writer.columns().size(), 19U);
  ASSERT_TRUE(writer.write_row_group(golden_records()).has_value());
  ASSERT_TRUE(writer.write_row_group(golden_records()).has_value());
  EXPECT_EQ(writer.rows(), 2 * golden_records().size());
  ASSERT_TRUE(writer.close().has_value());

  const std::vector<std::byte> file = file_bytes(folder / "records.parquet");
  ASSERT_GT(file.size(), 3 * kMagicBytes);
  EXPECT_TRUE(std::equal(kMagic.begin(), kMagic.end(), file.begin()));
  EXPECT_TRUE(std::equal(kMagic.begin(), kMagic.end(), file.end() - kMagicBytes));
  const std::uint32_t footer = footer_length(file);
  ASSERT_LT(footer, file.size() - 2 * kMagicBytes - 4);
  // The footer opens with its version field: 1, zigzag-encoded.
  const std::size_t footer_at = file.size() - kMagicBytes - 4 - footer;
  EXPECT_EQ(file[footer_at], std::byte{0x15});
  EXPECT_EQ(file[footer_at + 1], std::byte{0x02});
}

TEST(ParquetWriter, WritesTheSameBytesForTheSameRows) {
  const TempDir folder;
  for (const char* name : {"a.parquet", "b.parquet"}) {
    ParquetWriter writer = ParquetWriter::create(folder / name, *ics::v1::PliEvent::descriptor()).value();
    ASSERT_TRUE(writer.write_row_group(golden_events()).has_value());
    ASSERT_TRUE(writer.close().has_value());
  }
  EXPECT_EQ(file_bytes(folder / "a.parquet"), file_bytes(folder / "b.parquet"));
}

TEST(ParquetWriter, WritesNoRowGroupForNoRows) {
  const TempDir folder;
  ParquetWriter writer = ParquetWriter::create(folder / "empty.parquet", *ics::v1::PliEvent::descriptor()).value();
  ASSERT_TRUE(writer.write_row_group(std::vector<ics::v1::PliEvent>{}).has_value());
  EXPECT_EQ(writer.rows(), 0U);
  ASSERT_TRUE(writer.close().has_value());
  const std::vector<std::byte> file = file_bytes(folder / "empty.parquet");
  EXPECT_EQ(file.size(), 2 * kMagicBytes + 4 + footer_length(file));
}

TEST(ParquetWriter, RejectsRowsOfAnotherType) {
  const TempDir folder;
  ParquetWriter writer = ParquetWriter::create(folder / "records.parquet", *ics::v1::PliRecord::descriptor()).value();
  EXPECT_EQ(writer.write_row_group(golden_events()).error(), ics::Error::kInvalidArgument);
}

TEST(ParquetWriter, RefusesWorkOnceClosed) {
  const TempDir folder;
  ParquetWriter writer = ParquetWriter::create(folder / "records.parquet", *ics::v1::PliRecord::descriptor()).value();
  ASSERT_TRUE(writer.close().has_value());
  EXPECT_EQ(writer.write_row_group(golden_records()).error(), ics::Error::kInvalidArgument);
  EXPECT_EQ(writer.close().error(), ics::Error::kInvalidArgument);
}

TEST(ParquetWriter, ReportsAFileItCannotCreate) {
  const TempDir folder;
  std::ofstream(folder / "kept.parquet") << "kept";
  EXPECT_EQ(ParquetWriter::create(folder / "kept.parquet", *ics::v1::PliRecord::descriptor()).error(),
            ics::Error::kUnwritable);
  EXPECT_EQ(ParquetWriter::create(folder / "query.parquet", *ics::v1::QueryPliResponse::descriptor()).error(),
            ics::Error::kInvalidArgument);
  EXPECT_FALSE(std::filesystem::exists(folder / "query.parquet"));
}

TEST(ParquetWriter, ReportsAFailedWrite) {
  const TempDir folder;
  ParquetWriter writer = ParquetWriter::create(folder / "records.parquet", *ics::v1::PliRecord::descriptor()).value();
  {
    const ics::capture::testing::FileSizeLimit limit(kMagicBytes);
    EXPECT_EQ(writer.write_row_group(golden_records()).error(), ics::Error::kUnwritable);
    EXPECT_EQ(writer.close().error(), ics::Error::kUnwritable);
  }
  EXPECT_EQ(writer.rows(), 0U);
}

}  // namespace
