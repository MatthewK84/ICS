#include "ics/store/segment_format.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "ics/store/crc32c.hpp"
#include "store_support.hpp"

namespace {

using ics::store::append_entry;
using ics::store::EntryKind;
using ics::store::Found;
using ics::store::kEntryHeaderBytes;
using ics::store::kMaxPayloadBytes;
using ics::store::kSegmentMagic;
using ics::store::parse_entry;
using ics::store::parse_magic;
using ics::store::testing::event;
using ics::store::testing::record;

TEST(SegmentFormat, ParsesTheEntriesItAppends) {
  std::vector<std::byte> bytes;
  ASSERT_TRUE(append_entry(record(1), bytes).has_value());
  const std::size_t first = bytes.size();
  ASSERT_TRUE(append_entry(event(2), bytes).has_value());

  const ics::store::Parsed parsed = parse_entry(bytes);
  ASSERT_EQ(parsed.found, Found::kEntry);
  EXPECT_EQ(parsed.size, first);
  EXPECT_EQ(parsed.entry.kind, EntryKind::kRecord);
  ics::v1::PliRecord read;
  ASSERT_TRUE(read.ParseFromArray(parsed.entry.payload.data(), static_cast<int>(parsed.entry.payload.size())));
  EXPECT_EQ(read.SerializeAsString(), record(1).SerializeAsString());

  const ics::store::Parsed second = parse_entry(std::span(bytes).subspan(first));
  ASSERT_EQ(second.found, Found::kEntry);
  EXPECT_EQ(second.size, bytes.size() - first);
  EXPECT_EQ(second.entry.kind, EntryKind::kEvent);
  ics::v1::PliEvent read_event;
  ASSERT_TRUE(
      read_event.ParseFromArray(second.entry.payload.data(), static_cast<int>(second.entry.payload.size())));
  EXPECT_EQ(read_event.detail(), "status 2");
}

TEST(SegmentFormat, WritesTheDocumentedLayout) {
  std::vector<std::byte> bytes;
  ASSERT_TRUE(append_entry(ics::v1::PliEvent{}, bytes).has_value());
  // An empty event: length 0, then the CRC of the kind byte alone.
  const std::uint32_t crc = ics::store::crc32c(std::span(bytes).subspan(8, 1));
  const std::vector<std::byte> expected{
      std::byte{0},
      std::byte{0},
      std::byte{0},
      std::byte{0},
      static_cast<std::byte>(crc),
      static_cast<std::byte>(crc >> 8U),
      static_cast<std::byte>(crc >> 16U),
      static_cast<std::byte>(crc >> 24U),
      std::byte{2},
  };
  EXPECT_EQ(bytes, expected);
}

TEST(SegmentFormat, FindsEveryStrictPrefixIncomplete) {
  std::vector<std::byte> bytes;
  ASSERT_TRUE(append_entry(record(3), bytes).has_value());
  for (std::size_t size = 0; size < bytes.size(); ++size) {
    EXPECT_EQ(parse_entry(std::span(bytes).first(size)).found, Found::kIncomplete) << size;
  }
}

TEST(SegmentFormat, RejectsABadLengthKindOrCrc) {
  std::vector<std::byte> good;
  ASSERT_TRUE(append_entry(record(4), good).has_value());

  std::vector<std::byte> long_length = good;
  long_length[3] = std::byte{1};  // 16 MiB
  EXPECT_EQ(parse_entry(std::span(long_length).first(kEntryHeaderBytes)).found, Found::kInvalid);

  for (const std::byte kind : {std::byte{0}, std::byte{3}}) {
    std::vector<std::byte> bad_kind = good;
    bad_kind[8] = kind;
    EXPECT_EQ(parse_entry(bad_kind).found, Found::kInvalid);
  }
  for (const std::size_t at : {std::size_t{4}, std::size_t{8}, good.size() - 1}) {
    std::vector<std::byte> flipped = good;
    flipped[at] ^= std::byte{0x10};
    EXPECT_EQ(parse_entry(flipped).found, Found::kInvalid) << at;
  }
}

TEST(SegmentFormat, RefusesAMessageLargerThanAnEntryHolds) {
  std::vector<std::byte> bytes{std::byte{9}};
  ics::v1::PliEvent large = event(5);
  large.set_detail(std::string(kMaxPayloadBytes, 'x'));
  EXPECT_EQ(append_entry(large, bytes).error(), ics::Error::kInvalidArgument);
  ics::v1::PliRecord large_record = record(5);
  large_record.set_entity_id(std::string(kMaxPayloadBytes, 'x'));
  EXPECT_EQ(append_entry(large_record, bytes).error(), ics::Error::kInvalidArgument);
  EXPECT_EQ(bytes, std::vector<std::byte>{std::byte{9}});
}

TEST(SegmentFormat, ParsesTheLargestEntry) {
  std::vector<std::byte> bytes;
  ics::v1::PliEvent large;
  // Field 8, length-delimited: a 1-byte tag and a 3-byte length.
  large.set_detail(std::string(kMaxPayloadBytes - 4, 'x'));
  ASSERT_TRUE(append_entry(large, bytes).has_value());
  EXPECT_EQ(bytes.size(), ics::store::kMaxEntryBytes);
  EXPECT_EQ(parse_entry(bytes).found, Found::kEntry);
}

TEST(SegmentFormat, RecognisesTheMagic) {
  std::vector<std::byte> bytes(kSegmentMagic.begin(), kSegmentMagic.end());
  EXPECT_EQ(parse_magic(bytes), Found::kEntry);
  bytes.push_back(std::byte{0});
  EXPECT_EQ(parse_magic(bytes), Found::kEntry);
  EXPECT_EQ(parse_magic(std::span(bytes).first(5)), Found::kIncomplete);
  EXPECT_EQ(parse_magic({}), Found::kIncomplete);
  bytes[6] = std::byte{'2'};
  EXPECT_EQ(parse_magic(bytes), Found::kInvalid);
  EXPECT_EQ(parse_magic(std::span(bytes).first(6)), Found::kIncomplete);
}

}  // namespace
