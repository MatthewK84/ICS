#include "ics/lattice/sse.hpp"

#include <cstddef>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace ics::lattice {
namespace {

std::vector<SseEvent> read_all(const std::string_view stream) {
  SseReader reader;
  return reader.feed(stream);
}

// The stream fed one byte at a time.
std::vector<SseEvent> read_bytewise(const std::string_view stream) {
  SseReader reader;
  std::vector<SseEvent> out;
  for (std::size_t i = 0; i < stream.size(); ++i) {
    std::vector<SseEvent> events = reader.feed(stream.substr(i, 1));
    out.insert(out.end(), std::make_move_iterator(events.begin()), std::make_move_iterator(events.end()));
  }
  return out;
}

TEST(SseReader, ReadsEventsWithNamesAndJoinedData) {
  const std::vector<SseEvent> got = read_all("data: one\n\nevent: entity\ndata: a\ndata: b\n\n");
  ASSERT_EQ(got.size(), 2U);
  EXPECT_EQ(got[0].event, "");
  EXPECT_EQ(got[0].data, "one");
  EXPECT_EQ(got[1].event, "entity");
  EXPECT_EQ(got[1].data, "a\nb");
}

TEST(SseReader, TakesEveryLineEndingAndAnyChunking) {
  constexpr std::string_view kStream = "data: lf\n\ndata: crlf\r\n\r\ndata: cr\r\rdata: mixed\r\n\n";
  for (const std::vector<SseEvent>& got : {read_all(kStream), read_bytewise(kStream)}) {
    ASSERT_EQ(got.size(), 4U);
    EXPECT_EQ(got[0].data, "lf");
    EXPECT_EQ(got[1].data, "crlf");
    EXPECT_EQ(got[2].data, "cr");
    EXPECT_EQ(got[3].data, "mixed");
  }
}

TEST(SseReader, FollowsTheFieldRules) {
  const std::vector<SseEvent> got =
      read_all(": a comment\nid: 7\nretry: 10\ndata:tight\ndata:  two spaces\nunknown: x\n\ndata\n\n");
  ASSERT_EQ(got.size(), 2U);
  EXPECT_EQ(got[0].data, "tight\n two spaces");
  EXPECT_EQ(got[1].data, "");
}

TEST(SseReader, DispatchesNoEventWithoutData) {
  EXPECT_TRUE(read_all("event: heartbeat\n\n: keep-alive\n\n\n").empty());
  // An event the stream has not finished yet.
  EXPECT_TRUE(read_all("data: unfinished\n").empty());
}

TEST(SseReader, DropsAByteOrderMarkOnlyAtTheStart) {
  const std::vector<SseEvent> got = read_all("\xEF\xBB\xBF" "data: first\n\n\xEF\xBB\xBF" "data: second\n\n");
  ASSERT_EQ(got.size(), 1U);
  EXPECT_EQ(got[0].data, "first");
}

TEST(SseReader, DropsAnEventPastTheLimitAndReadsTheNext) {
  SseReader reader(16);
  EXPECT_TRUE(reader.feed("data: 0123456789abcdef\n\n").empty());
  EXPECT_TRUE(reader.feed("data: 01234567\ndata: 89abcdef\n\n").empty());
  EXPECT_TRUE(reader.feed("event: 0123456789abcdef\ndata: x\n\n").empty());
  EXPECT_EQ(reader.oversized(), 3U);
  const std::vector<SseEvent> got = reader.feed("data: fits\n\n");
  ASSERT_EQ(got.size(), 1U);
  EXPECT_EQ(got[0].data, "fits");
}

}  // namespace
}  // namespace ics::lattice
