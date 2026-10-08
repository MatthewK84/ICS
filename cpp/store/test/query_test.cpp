#include "ics/store/query.hpp"

#include <cstddef>
#include <cstdint>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "ics/common/units.hpp"
#include "ics/store/crc32c.hpp"
#include "ics/store/segment_format.hpp"
#include "ics/store/segment_writer.hpp"
#include "store_support.hpp"
#include "support.hpp"

namespace {

using ics::store::answer;
using ics::store::Catalog;
using ics::store::QueryLimits;
using ics::store::SegmentWriter;
using ics::store::testing::event;
using ics::store::testing::kStartNs;
using ics::store::testing::record;
using ics::timing::testing::TempDir;
using ics::v1::QueryPliRequest;
using ics::v1::QueryPliResponse;

constexpr std::int64_t kMs = 1'000'000;
const ics::UtcTime kStart = ics::utc_from_ns(kStartNs);

// Every response a query sends.
struct Answered {
  std::vector<QueryPliResponse> responses;
  std::uint64_t sent = 0;
};

Answered ask(const QueryPliRequest& request, Catalog& catalog, const QueryLimits& limits = {}) {
  Answered out;
  out.sent = answer(request, catalog, limits, [&out](const QueryPliResponse& response) {
    out.responses.push_back(response);
    return true;
  });
  return out;
}

QueryPliRequest request(const QueryPliRequest::Kind kind, const std::int64_t start_ms, const std::int64_t end_ms) {
  QueryPliRequest out;
  out.set_kind(kind);
  out.set_start_utc_ns(kStartNs + (start_ms * kMs));
  out.set_end_utc_ns(kStartNs + (end_ms * kMs));
  return out;
}

// The times, in ms after kStartNs, of the records or events in responses.
std::vector<std::int64_t> times(const Answered& answered) {
  std::vector<std::int64_t> out;
  for (const QueryPliResponse& response : answered.responses) {
    for (const ics::v1::PliRecord& record : response.records()) {
      out.push_back((record.valid_utc_ns() - kStartNs) / kMs);
    }
    for (const ics::v1::PliEvent& event : response.events()) {
      out.push_back((event.time_utc_ns() - kStartNs) / kMs);
    }
  }
  return out;
}

// Two segments: records 0 to 9 and events 0 to 4 in the first, then records
// 10 to 19, written latest first, in the second.
void write_store(const std::filesystem::path& folder) {
  SegmentWriter writer = SegmentWriter::open(folder, kStart).value();
  for (int index = 0; index < 10; ++index) {
    ASSERT_TRUE(writer.append(record(index)).has_value());
    if (index < 5) {
      ASSERT_TRUE(writer.append(event(index)).has_value());
    }
  }
  ASSERT_TRUE(writer.rotate(kStart + std::chrono::hours(1)).has_value());
  for (int index = 19; index >= 10; --index) {
    ASSERT_TRUE(writer.append(record(index)).has_value());
  }
  ASSERT_TRUE(writer.close().has_value());
}

TEST(Answer, ReturnsRecordsInRangeEarliestFirst) {
  const TempDir folder;
  write_store(folder / "");
  Catalog catalog(folder / "");
  const Answered answered = ask(request(QueryPliRequest::KIND_RECORDS, 5, 15), catalog);
  EXPECT_EQ(times(answered), (std::vector<std::int64_t>{5, 6, 7, 8, 9, 10, 11, 12, 13, 14}));
  EXPECT_EQ(answered.sent, 10U);
  ASSERT_EQ(answered.responses.size(), 1U);
  EXPECT_TRUE(answered.responses[0].done());
  EXPECT_FALSE(answered.responses[0].truncated());
  EXPECT_TRUE(answered.responses[0].error().empty());
}

TEST(Answer, ReturnsEventsOfOneEntity) {
  const TempDir folder;
  write_store(folder / "");
  Catalog catalog(folder / "");
  QueryPliRequest events = request(QueryPliRequest::KIND_EVENTS, 0, 100);
  EXPECT_EQ(times(ask(events, catalog)), (std::vector<std::int64_t>{0, 1, 2, 3, 4}));
  events.set_entity_id("uav-1");
  EXPECT_EQ(times(ask(events, catalog)), (std::vector<std::int64_t>{1, 4}));
}

TEST(Answer, SkipsSegmentsOutsideTheRange) {
  const TempDir folder;
  write_store(folder / "");
  Catalog catalog(folder / "");
  EXPECT_EQ(times(ask(request(QueryPliRequest::KIND_RECORDS, 15, 100), catalog)),
            (std::vector<std::int64_t>{15, 16, 17, 18, 19}));
  EXPECT_EQ(times(ask(request(QueryPliRequest::KIND_RECORDS, -100, 3), catalog)),
            (std::vector<std::int64_t>{0, 1, 2}));
  EXPECT_TRUE(times(ask(request(QueryPliRequest::KIND_EVENTS, 10, 20), catalog)).empty());
  const Answered none = ask(request(QueryPliRequest::KIND_RECORDS, 100, 200), catalog);
  ASSERT_EQ(none.responses.size(), 1U);
  EXPECT_TRUE(none.responses[0].done());
  EXPECT_EQ(none.sent, 0U);
}

TEST(Answer, KeepsTheEarliestUpToTheLimit) {
  const TempDir folder;
  write_store(folder / "");
  Catalog catalog(folder / "");
  QueryPliRequest limited = request(QueryPliRequest::KIND_RECORDS, 0, 100);
  limited.set_limit_count(4);
  const Answered answered = ask(limited, catalog);
  EXPECT_EQ(times(answered), (std::vector<std::int64_t>{0, 1, 2, 3}));
  EXPECT_TRUE(answered.responses.back().truncated());
  // The server's own limit caps a larger one, and a limit of 0 means it.
  limited.set_limit_count(1000);
  EXPECT_EQ(times(ask(limited, catalog, {.max_items = 3})), (std::vector<std::int64_t>{0, 1, 2}));
  limited.set_limit_count(0);
  EXPECT_EQ(times(ask(limited, catalog, {.max_items = 2})), (std::vector<std::int64_t>{0, 1}));
}

TEST(Answer, KeepsAnEarlierMatchFoundLate) {
  const TempDir folder;
  SegmentWriter writer = SegmentWriter::open(folder / "", kStart).value();
  for (const int index : {5, 6, 1}) {
    ASSERT_TRUE(writer.append(record(index)).has_value());
    ASSERT_TRUE(writer.append(event(index)).has_value());
  }
  ASSERT_TRUE(writer.close().has_value());
  Catalog catalog(folder / "");
  for (const QueryPliRequest::Kind kind : {QueryPliRequest::KIND_RECORDS, QueryPliRequest::KIND_EVENTS}) {
    QueryPliRequest two = request(kind, 0, 100);
    two.set_limit_count(2);
    const Answered answered = ask(two, catalog);
    EXPECT_EQ(times(answered), (std::vector<std::int64_t>{1, 5})) << kind;
    EXPECT_TRUE(answered.responses.back().truncated());
  }
}

TEST(Answer, SendsEventsInBatchesToo) {
  const TempDir folder;
  write_store(folder / "");
  Catalog catalog(folder / "");
  const Answered answered = ask(request(QueryPliRequest::KIND_EVENTS, 0, 20), catalog,
                                {.max_items = 100, .max_response_bytes = event(0).ByteSizeLong() + 7});
  EXPECT_EQ(answered.responses.size(), 5U);
  EXPECT_EQ(answered.sent, 5U);
}

TEST(Answer, KeepsStoreOrderAtTheSameTime) {
  const TempDir folder;
  SegmentWriter writer = SegmentWriter::open(folder / "", kStart).value();
  for (const char* entity : {"first", "second", "third"}) {
    ics::v1::PliRecord same = record(0);
    same.set_entity_id(entity);
    ASSERT_TRUE(writer.append(same).has_value());
  }
  ASSERT_TRUE(writer.close().has_value());
  Catalog catalog(folder / "");
  QueryPliRequest two = request(QueryPliRequest::KIND_RECORDS, 0, 1);
  two.set_limit_count(2);
  const Answered answered = ask(two, catalog);
  ASSERT_EQ(answered.responses.size(), 1U);
  ASSERT_EQ(answered.responses[0].records_size(), 2);
  EXPECT_EQ(answered.responses[0].records(0).entity_id(), "first");
  EXPECT_EQ(answered.responses[0].records(1).entity_id(), "second");
}

TEST(Answer, SendsBatchesUnderTheSizeLimit) {
  const TempDir folder;
  write_store(folder / "");
  Catalog catalog(folder / "");
  const std::size_t one = record(0).ByteSizeLong() + 6;
  const Answered answered = ask(request(QueryPliRequest::KIND_RECORDS, 0, 20), catalog,
                                {.max_items = 100, .max_response_bytes = (3 * one) + 1});
  EXPECT_EQ(answered.sent, 20U);
  EXPECT_EQ(times(answered).size(), 20U);
  ASSERT_GT(answered.responses.size(), 6U);
  for (std::size_t index = 0; index + 1 < answered.responses.size(); ++index) {
    EXPECT_FALSE(answered.responses[index].done());
    EXPECT_LE(answered.responses[index].ByteSizeLong(), (3 * one) + 1);
  }
  EXPECT_TRUE(answered.responses.back().done());
}

TEST(Answer, StopsWhenAResponseCannotBeSent) {
  const TempDir folder;
  write_store(folder / "");
  Catalog catalog(folder / "");
  const std::size_t one = record(0).ByteSizeLong() + 6;
  for (const int allowed : {0, 1}) {
    int calls = 0;
    const std::uint64_t sent =
        answer(request(QueryPliRequest::KIND_RECORDS, 0, 20), catalog, {.max_items = 100, .max_response_bytes = one},
               [&calls, allowed](const QueryPliResponse&) { return calls++ < allowed; });
    EXPECT_EQ(sent, static_cast<std::uint64_t>(allowed));
    EXPECT_EQ(calls, allowed + 1);
  }
  int last = 0;
  const std::uint64_t sent = answer(request(QueryPliRequest::KIND_RECORDS, 0, 1), catalog, {},
                                    [&last](const QueryPliResponse&) { return ++last > 1; });
  EXPECT_EQ(sent, 0U);
}

TEST(Answer, RefusesARequestItCannotAnswer) {
  const TempDir folder;
  Catalog catalog(folder / "");
  for (const QueryPliRequest& bad : {request(QueryPliRequest::KIND_UNSPECIFIED, 0, 1),
                                     request(QueryPliRequest::KIND_EVENTS, 1, 1),
                                     request(QueryPliRequest::KIND_RECORDS, 2, 1)}) {
    const Answered answered = ask(bad, catalog);
    ASSERT_EQ(answered.responses.size(), 1U);
    EXPECT_TRUE(answered.responses[0].done());
    EXPECT_FALSE(answered.responses[0].error().empty());
    EXPECT_EQ(answered.sent, 0U);
  }
}

TEST(Catalog, ReadsAgainOnlyASegmentThatGrew) {
  const TempDir folder;
  SegmentWriter writer = SegmentWriter::open(folder / "", kStart).value();
  ASSERT_TRUE(writer.append(record(0)).has_value());
  ASSERT_TRUE(writer.sync().has_value());
  Catalog catalog(folder / "");
  ASSERT_EQ(catalog.segments().size(), 1U);
  EXPECT_EQ(catalog.segments()[0].summary.records, 1U);
  ASSERT_TRUE(writer.append(record(5)).has_value());
  ASSERT_TRUE(writer.append(event(2)).has_value());
  ASSERT_TRUE(writer.sync().has_value());
  const ics::store::SegmentSummary summary = catalog.segments()[0].summary;
  EXPECT_EQ(summary.records, 2U);
  EXPECT_EQ(summary.first_record_ns, kStartNs);
  EXPECT_EQ(summary.last_record_ns, kStartNs + (5 * kMs));
  EXPECT_EQ(summary.events, 1U);
  EXPECT_EQ(summary.first_event_ns, kStartNs + (2 * kMs));
}

TEST(Catalog, LeavesOutWhatIsNotAReadableSegment) {
  const TempDir folder;
  write_store(folder / "");
  std::filesystem::create_directory(folder / ics::store::segment_name(kStart + std::chrono::hours(5)));
  std::ofstream(folder / "notes.txt") << "not a segment";
  Catalog catalog(folder / "");
  EXPECT_EQ(catalog.segments().size(), 2U);
  EXPECT_TRUE(Catalog(folder / "missing").segments().empty());
}

TEST(Answer, SkipsEntriesThatDoNotHoldTheirMessage) {
  const TempDir folder;
  SegmentWriter writer = SegmentWriter::open(folder / "", kStart).value();
  ASSERT_TRUE(writer.append(record(1)).has_value());
  ASSERT_TRUE(writer.append(event(1)).has_value());
  ASSERT_TRUE(writer.close().has_value());
  // Append a sound entry of each kind whose payload ends inside a tag.
  std::vector<std::byte> bytes = ics::store::testing::file_bytes(writer.path());
  for (const std::byte kind : {std::byte{1}, std::byte{2}}) {
    const std::vector<std::byte> body{kind, std::byte{0xFF}};
    const std::uint32_t crc = ics::store::crc32c(body);
    const std::vector<std::byte> header{std::byte{1}, std::byte{0}, std::byte{0}, std::byte{0},
                                        static_cast<std::byte>(crc), static_cast<std::byte>(crc >> 8U),
                                        static_cast<std::byte>(crc >> 16U), static_cast<std::byte>(crc >> 24U)};
    bytes.insert(bytes.end(), header.begin(), header.end());
    bytes.insert(bytes.end(), body.begin(), body.end());
  }
  ics::store::testing::write_file(writer.path(), bytes);
  Catalog catalog(folder / "");
  EXPECT_EQ(times(ask(request(QueryPliRequest::KIND_RECORDS, 0, 10), catalog)), std::vector<std::int64_t>{1});
  EXPECT_EQ(times(ask(request(QueryPliRequest::KIND_EVENTS, 0, 10), catalog)), std::vector<std::int64_t>{1});
}

}  // namespace
