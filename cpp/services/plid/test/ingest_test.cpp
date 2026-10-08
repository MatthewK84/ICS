#include "ics/plid/ingest.hpp"

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>

#include <gtest/gtest.h>

#include "capture_support.hpp"
#include "ics/store/segment_reader.hpp"
#include "ics/store/segment_writer.hpp"
#include "plid_support.hpp"
#include "support.hpp"

namespace {

using ics::plid::Ingest;
using ics::plid::Pli;
using ics::plid::testing::kStart;
using ics::timing::testing::TempDir;
using std::chrono::hours;
using std::chrono::milliseconds;
using std::chrono::seconds;

constexpr ics::plid::IngestTiming kTiming{.rotate_interval = hours(1), .sync_interval = seconds(1)};

ics::v1::PliEvent arming(const std::string& entity, const ics::v1::PliEvent::Kind kind) {
  ics::v1::PliEvent event;
  event.set_entity_id(entity);
  event.set_kind(kind);
  return event;
}

std::uint64_t entries(const std::filesystem::path& segment) {
  std::uint64_t count = 0;
  static_cast<void>(ics::store::read_segment(segment, [&count](const ics::store::Entry&) { ++count; }));
  return count;
}

TEST(Ingest, WritesEachTickAndSyncsOnceTheIntervalPasses) {
  const TempDir folder;
  Ingest ingest = Ingest::open(folder / "", kTiming, kStart).value();
  Pli pli;
  pli.records.resize(2);
  pli.events.resize(1);
  ASSERT_TRUE(ingest.store(pli).has_value());
  EXPECT_TRUE(pli.records.empty());
  EXPECT_TRUE(pli.events.empty());
  EXPECT_EQ(ingest.stored(), 3U);
  EXPECT_EQ(entries(ingest.path()), 0U);
  EXPECT_FALSE(ingest.tick(kStart + milliseconds(999)).value().has_value());
  EXPECT_EQ(entries(ingest.path()), 3U);
  pli.records.resize(1);
  ASSERT_TRUE(ingest.store(pli).has_value());
  EXPECT_FALSE(ingest.tick(kStart + seconds(1)).value().has_value());
  EXPECT_EQ(entries(ingest.path()), 4U);
}

TEST(Ingest, RotatesOnceTheSegmentIsOld) {
  const TempDir folder;
  Ingest ingest = Ingest::open(folder / "", kTiming, kStart).value();
  const std::filesystem::path first = ingest.path();
  EXPECT_EQ(ingest.tick(kStart + hours(1)).value(), std::optional(first));
  EXPECT_NE(ingest.path(), first);
  EXPECT_EQ(ingest.close().value(), folder / ics::store::segment_name(kStart + hours(1)));
}

TEST(Ingest, RotatesWhenTheLastArmedVehicleDisarms) {
  const TempDir folder;
  Ingest ingest = Ingest::open(folder / "", kTiming, kStart).value();
  Pli pli;
  pli.events = {arming("1", ics::v1::PliEvent::KIND_ARMED), arming("2", ics::v1::PliEvent::KIND_ARMED),
                arming("3", ics::v1::PliEvent::KIND_DISARMED), arming("1", ics::v1::PliEvent::KIND_DISARMED),
                arming("1", ics::v1::PliEvent::KIND_MODE_CHANGED)};
  ASSERT_TRUE(ingest.store(pli).has_value());
  // Vehicle 2 is still armed.
  EXPECT_FALSE(ingest.tick(kStart).value().has_value());
  pli.events = {arming("2", ics::v1::PliEvent::KIND_DISARMED), arming("4", ics::v1::PliEvent::KIND_ARMED),
                arming("4", ics::v1::PliEvent::KIND_DISARMED)};
  ASSERT_TRUE(ingest.store(pli).has_value());
  EXPECT_TRUE(ingest.tick(kStart + seconds(5)).value().has_value());
  // One rotation for the sortie, however many vehicles ended it.
  EXPECT_FALSE(ingest.tick(kStart + seconds(5)).value().has_value());
}

TEST(Ingest, DropsAndCountsWhatIsTooLargeToStore) {
  const TempDir folder;
  Ingest ingest = Ingest::open(folder / "", kTiming, kStart).value();
  Pli pli;
  pli.events.resize(1);
  pli.events[0].set_detail(std::string(std::size_t{1} << 21U, 'x'));
  ASSERT_TRUE(ingest.store(pli).has_value());
  EXPECT_EQ(ingest.dropped(), 1U);
  EXPECT_EQ(ingest.stored(), 0U);
}

TEST(Ingest, ReportsWhatItCannotWrite) {
  const TempDir folder;
  EXPECT_EQ(Ingest::open(folder / "missing", kTiming, kStart).error(), ics::Error::kUnwritable);
  Ingest ingest = Ingest::open(folder / "", kTiming, kStart).value();
  Pli pli;
  pli.records.resize(1);
  ASSERT_TRUE(ingest.store(pli).has_value());
  const ics::capture::testing::FileSizeLimit limit(8);
  EXPECT_EQ(ingest.tick(kStart + seconds(1)).error(), ics::Error::kUnwritable);
  // A full write buffer fails the store itself.
  pli.records.resize(20'000);
  for (ics::v1::PliRecord& record : pli.records) {
    record.set_entity_id(std::string(100, 'e'));
  }
  EXPECT_EQ(ingest.store(pli).error(), ics::Error::kUnwritable);
}

}  // namespace
