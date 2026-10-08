#include "ics/plid/archiver.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

#include "ics/store/archive.hpp"
#include "ics/store/segment_writer.hpp"
#include "plid_support.hpp"
#include "support.hpp"

namespace {

using ics::plid::Archiver;
using ics::plid::testing::kStart;
using ics::plid::testing::Logged;
using ics::timing::testing::TempDir;

std::filesystem::path closed_segment(const std::filesystem::path& folder, const ics::UtcTime opened) {
  ics::store::SegmentWriter writer = ics::store::SegmentWriter::open(folder, opened).value();
  ics::v1::PliRecord record;
  record.set_entity_id("1");
  EXPECT_TRUE(writer.append(record).has_value());
  return writer.close().value();
}

TEST(Archiver, ArchivesEachSegmentAdded) {
  const TempDir folder;
  Logged logged;
  {
    Archiver archiver(logged.logger, {});
    const std::filesystem::path first = closed_segment(folder / "", kStart);
    const std::filesystem::path second = closed_segment(folder / "", kStart + std::chrono::hours(1));
    archiver.add(first);
    archiver.add(second);
    archiver.wait_idle();
    EXPECT_EQ(archiver.archived(), 2U);
    EXPECT_EQ(archiver.failed(), 0U);
    EXPECT_TRUE(ics::store::is_archived(first));
    EXPECT_TRUE(ics::store::is_archived(second));
  }
  EXPECT_TRUE(logged.has(R"("event":"archived")"));
  EXPECT_TRUE(logged.has(R"("records":1,"events":0)"));
}

TEST(Archiver, WarnsOfASegmentItCannotArchive) {
  const TempDir folder;
  Logged logged;
  {
    Archiver archiver(logged.logger, {});
    archiver.add(folder / "missing.icspli");
    archiver.wait_idle();
    EXPECT_EQ(archiver.failed(), 1U);
  }
  EXPECT_TRUE(logged.has(R"("event":"archive_failed")"));
  EXPECT_TRUE(logged.has(R"("error":"unreadable")"));
}

TEST(Archiver, FinishesItsQueueBeforeItStops) {
  const TempDir folder;
  Logged logged;
  const std::filesystem::path segment = closed_segment(folder / "", kStart);
  {
    Archiver archiver(logged.logger, {});
    archiver.add(segment);
  }
  EXPECT_TRUE(ics::store::is_archived(segment));
}

}  // namespace
