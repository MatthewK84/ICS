#include "ics/sapient/stream.hpp"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <span>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "framing.hpp"

namespace ics::sapient {
namespace {

using testing::frame;

Message message(const std::string& node_id) {
  Message out;
  out.set_node_id(node_id);
  out.mutable_timestamp()->set_seconds(1'791'115'200);
  out.mutable_detection_report()->set_object_id("01K6PZ0000000000000000B001");
  return out;
}

std::vector<std::byte> stream_of(const std::vector<Message>& messages) {
  std::vector<std::byte> out;
  for (const Message& one : messages) {
    const std::vector<std::byte> framed = frame(one);
    out.insert(out.end(), framed.begin(), framed.end());
  }
  return out;
}

std::vector<std::byte> header(const std::size_t length) {
  return {static_cast<std::byte>(length & 0xFFU), static_cast<std::byte>((length >> 8U) & 0xFFU),
          static_cast<std::byte>((length >> 16U) & 0xFFU), static_cast<std::byte>((length >> 24U) & 0xFFU)};
}

TEST(SapientStream, ReadsMessagesInChunksOfAnySize) {
  const std::vector<Message> sent{message("a"), message("b"), Message(), message("c")};
  const std::vector<std::byte> stream = stream_of(sent);
  for (std::size_t chunk = 1; chunk <= stream.size(); ++chunk) {
    StreamReader reader;
    std::vector<Message> read;
    for (std::size_t at = 0; at < stream.size(); at += chunk) {
      std::vector<Message> fed = reader.feed(std::span(stream).subspan(at, std::min(chunk, stream.size() - at)));
      read.insert(read.end(), std::make_move_iterator(fed.begin()), std::make_move_iterator(fed.end()));
    }
    ASSERT_EQ(read.size(), sent.size()) << chunk;
    for (std::size_t i = 0; i < sent.size(); ++i) {
      EXPECT_EQ(read[i].SerializeAsString(), sent[i].SerializeAsString()) << chunk << " " << i;
    }
    EXPECT_FALSE(reader.broken());
  }
}

TEST(SapientStream, WaitsForTheRestOfAMessage) {
  const std::vector<std::byte> stream = stream_of({message("a")});
  StreamReader reader;
  EXPECT_TRUE(reader.feed(std::span(stream).first(stream.size() - 1)).empty());
  EXPECT_TRUE(reader.feed({}).empty());
  EXPECT_EQ(reader.feed(std::span(stream).last(1)).size(), 1U);
}

TEST(SapientStream, TakesAMessageOfTheLargestLength) {
  // Field 13, additional_information, holding 1,048,572 bytes: a varint
  // length of 3 bytes makes the message exactly 1 MiB.
  constexpr std::size_t kText = StreamReader::kMaxMessageBytes - 4;
  std::vector<std::byte> stream = header(StreamReader::kMaxMessageBytes);
  for (const unsigned value : {0x6AU, 0xFCU, 0xFFU, 0x3FU}) {
    stream.push_back(static_cast<std::byte>(value));
  }
  stream.resize(stream.size() + kText, std::byte{'x'});
  StreamReader reader;
  const std::vector<Message> read = reader.feed(stream);
  ASSERT_EQ(read.size(), 1U);
  EXPECT_EQ(read[0].additional_information().size(), kText);
  EXPECT_FALSE(reader.broken());
}

TEST(SapientStream, BreaksOnALengthPastTheLimit) {
  std::vector<std::byte> stream = stream_of({message("a")});
  const std::vector<std::byte> oversized = header(StreamReader::kMaxMessageBytes + 1);
  stream.insert(stream.end(), oversized.begin(), oversized.end());
  const std::vector<std::byte> after = stream_of({message("b")});
  stream.insert(stream.end(), after.begin(), after.end());
  StreamReader reader;
  const std::vector<Message> read = reader.feed(stream);
  ASSERT_EQ(read.size(), 1U);
  EXPECT_EQ(read[0].node_id(), "a");
  EXPECT_TRUE(reader.broken());
  EXPECT_TRUE(reader.feed(stream_of({message("c")})).empty());
}

TEST(SapientStream, BreaksOnAMessageThatDoesNotParse) {
  // Field 1, the timestamp, as a length-delimited field longer than the
  // message.
  std::vector<std::byte> stream = header(2);
  stream.push_back(std::byte{0x0A});
  stream.push_back(std::byte{0x7F});
  const std::vector<std::byte> after = stream_of({message("b")});
  stream.insert(stream.end(), after.begin(), after.end());
  StreamReader reader;
  EXPECT_TRUE(reader.feed(stream).empty());
  EXPECT_TRUE(reader.broken());
}

}  // namespace
}  // namespace ics::sapient
