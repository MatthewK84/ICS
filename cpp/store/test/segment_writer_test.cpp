#include "ics/store/segment_writer.hpp"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

#include "capture_support.hpp"
#include "ics/common/units.hpp"
#include "ics/store/segment_format.hpp"
#include "ics/store/segment_reader.hpp"
#include "store_support.hpp"
#include "support.hpp"

namespace {

using ics::store::is_segment_name;
using ics::store::segment_name;
using ics::store::SegmentWriter;
using ics::store::testing::event;
using ics::store::testing::file_bytes;
using ics::store::testing::kStartNs;
using ics::store::testing::record;
using ics::timing::testing::TempDir;
using std::chrono::hours;
using std::chrono::minutes;

const ics::UtcTime kStart = ics::utc_from_ns(kStartNs);

TEST(SegmentName, NamesASegmentForItsTime) {
  // 1'790'000'000 s is 2026-09-21T14:13:20Z.
  EXPECT_EQ(segment_name(kStart + ics::Duration(123)), "pli-20260921T141320.000000123Z.icspli");
  EXPECT_TRUE(is_segment_name(segment_name(kStart)));
}

TEST(SegmentName, RecognisesOnlySegmentNames) {
  EXPECT_FALSE(is_segment_name("pli-20260921T133320.000000123Z.icspl"));
  EXPECT_FALSE(is_segment_name("pli-2026092aT133320.000000123Z.icspli"));
  EXPECT_FALSE(is_segment_name("pli-2026092/T133320.000000123Z.icspli"));
  EXPECT_FALSE(is_segment_name("pli-20260921X133320.000000123Z.icspli"));
  EXPECT_FALSE(is_segment_name("pli-20260921T133320.000000123Z.parquet"));
}

TEST(SegmentWriter, StartsEachSegmentWithTheMagic) {
  const TempDir folder;
  SegmentWriter writer = SegmentWriter::open(folder / "", kStart).value();
  EXPECT_EQ(writer.path(), folder / segment_name(kStart));
  const std::vector<std::byte> magic(ics::store::kSegmentMagic.begin(), ics::store::kSegmentMagic.end());
  EXPECT_EQ(file_bytes(writer.path()), magic);
}

TEST(SegmentWriter, BuffersEntriesUntilSync) {
  const TempDir folder;
  SegmentWriter writer = SegmentWriter::open(folder / "", kStart).value();
  ASSERT_TRUE(writer.append(record(0)).has_value());
  ASSERT_TRUE(writer.append(event(1)).has_value());
  EXPECT_EQ(std::filesystem::file_size(writer.path()), ics::store::kSegmentMagic.size());
  ASSERT_TRUE(writer.sync().has_value());
  ics::store::SegmentReader reader = ics::store::SegmentReader::open(writer.path()).value();
  EXPECT_EQ(reader.skip_to_end().value(), std::filesystem::file_size(writer.path()));
  EXPECT_GT(reader.sound_bytes(), ics::store::kSegmentMagic.size());
}

TEST(SegmentWriter, WritesItsBufferWhenFull) {
  const TempDir folder;
  SegmentWriter writer = SegmentWriter::open(folder / "", kStart).value();
  int index = 0;
  for (; std::filesystem::file_size(writer.path()) == ics::store::kSegmentMagic.size(); ++index) {
    ASSERT_TRUE(writer.append(record(index)).has_value());
    ASSERT_LT(index, 100'000);
  }
  EXPECT_GE(std::filesystem::file_size(writer.path()), SegmentWriter::kFlushBytes);
}

TEST(SegmentWriter, RotatesAfterItsMaximumAge) {
  const TempDir folder;
  SegmentWriter writer = SegmentWriter::open(folder / "", kStart).value();
  EXPECT_FALSE(writer.due(kStart + minutes(59), hours(1)));
  EXPECT_TRUE(writer.due(kStart + hours(1), hours(1)));
  ASSERT_TRUE(writer.append(record(0)).has_value());
  const std::filesystem::path closed = writer.rotate(kStart + hours(1)).value();
  EXPECT_EQ(closed, folder / segment_name(kStart));
  EXPECT_GT(std::filesystem::file_size(closed), ics::store::kSegmentMagic.size());
  EXPECT_EQ(writer.path(), folder / segment_name(kStart + hours(1)));
  EXPECT_FALSE(writer.due(kStart + hours(1), hours(1)));
}

TEST(SegmentWriter, NamesASegmentOpenedInTheSameNanosecond1NsLater) {
  const TempDir folder;
  SegmentWriter writer = SegmentWriter::open(folder / "", kStart).value();
  ASSERT_TRUE(writer.rotate(kStart).has_value());
  EXPECT_EQ(writer.path(), folder / segment_name(kStart + ics::Duration(1)));
}

TEST(SegmentWriter, RefusesWorkOnceClosed) {
  const TempDir folder;
  SegmentWriter writer = SegmentWriter::open(folder / "", kStart).value();
  const std::filesystem::path closed = writer.close().value();
  EXPECT_EQ(writer.path(), closed);
  EXPECT_EQ(writer.append(record(0)).error(), ics::Error::kInvalidArgument);
  EXPECT_EQ(writer.append(event(0)).error(), ics::Error::kInvalidArgument);
  EXPECT_EQ(writer.sync().error(), ics::Error::kInvalidArgument);
  EXPECT_EQ(writer.close().error(), ics::Error::kInvalidArgument);
  EXPECT_FALSE(writer.due(kStart + hours(2), hours(1)));
}

TEST(SegmentWriter, RefusesAnEntryTooLarge) {
  const TempDir folder;
  SegmentWriter writer = SegmentWriter::open(folder / "", kStart).value();
  ics::v1::PliEvent large = event(0);
  large.set_detail(std::string(ics::store::kMaxPayloadBytes, 'x'));
  EXPECT_EQ(writer.append(large).error(), ics::Error::kInvalidArgument);
}

TEST(SegmentWriter, NeverOverwritesASegment) {
  const TempDir folder;
  std::ofstream(folder / segment_name(kStart)) << "kept";
  EXPECT_EQ(SegmentWriter::open(folder / "", kStart).error(), ics::Error::kUnwritable);
  EXPECT_EQ(SegmentWriter::open(folder / "missing", kStart).error(), ics::Error::kUnwritable);
}

TEST(SegmentWriter, ReportsAFailedWrite) {
  const TempDir folder;
  SegmentWriter writer = SegmentWriter::open(folder / "", kStart).value();
  ASSERT_TRUE(writer.append(record(0)).has_value());
  {
    const ics::capture::testing::FileSizeLimit limit(ics::store::kSegmentMagic.size());
    EXPECT_EQ(writer.rotate(kStart + hours(1)).error(), ics::Error::kUnwritable);
  }
  EXPECT_EQ(writer.append(record(1)).error(), ics::Error::kInvalidArgument);
}

TEST(SegmentWriter, ReportsAFullBufferItCannotWrite) {
  const TempDir folder;
  SegmentWriter writer = SegmentWriter::open(folder / "", kStart).value();
  const ics::capture::testing::FileSizeLimit limit(ics::store::kSegmentMagic.size());
  ics::Status appended;
  for (int index = 0; appended && index < 100'000; ++index) {
    appended = writer.append(record(index));
  }
  EXPECT_EQ(appended.error(), ics::Error::kUnwritable);
}

TEST(SegmentWriter, ReportsANextSegmentItCannotOpen) {
  const TempDir folder;
  const std::filesystem::path inner = folder / "inner";
  std::filesystem::create_directory(inner);
  SegmentWriter writer = SegmentWriter::open(inner, kStart).value();
  std::filesystem::remove_all(inner);
  EXPECT_EQ(writer.rotate(kStart + hours(1)).error(), ics::Error::kUnwritable);
}

}  // namespace
