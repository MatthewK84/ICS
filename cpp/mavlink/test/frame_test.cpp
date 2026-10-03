#include "ics/mavlink/frame.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "ics/mavlink/messages.hpp"
#include "mavlink_support.hpp"

namespace ics::mavlink {
namespace {

using testing::Bytes;
using testing::concat;
using testing::frame_v1;
using testing::frame_v2;
using testing::from_hex;

std::vector<Frame> read_all(const Bytes& datagram, ReadCounts* counts = nullptr) {
  FrameReader reader(datagram);
  std::vector<Frame> frames;
  for (std::optional<Frame> frame = reader.next(); frame; frame = reader.next()) {
    frames.push_back(*frame);
  }
  if (counts != nullptr) {
    *counts = reader.counts();
  }
  return frames;
}

TEST(X25, MatchesTheStandardCheckValue) {
  // CRC-16/MCRF4XX of "123456789".
  const Bytes digits = from_hex("313233343536373839");
  EXPECT_EQ(x25(digits), 0x6F91);
  EXPECT_EQ(x25({}), 0xFFFF);
}

TEST(CrcExtra, KnowsOnlyTheMessagesIcsReads) {
  EXPECT_EQ(crc_extra(kHeartbeatId), 50);
  EXPECT_EQ(crc_extra(kGlobalPositionIntId), 104);
  EXPECT_EQ(crc_extra(kStatusTextId), 83);
  EXPECT_EQ(crc_extra(1), std::nullopt);
  EXPECT_EQ(crc_extra(0x10000), std::nullopt);
}

TEST(FrameReader, ReadsEveryFrameTheRigEncodes) {
  const std::array<std::string_view, 8> hex{testing::kHeartbeat,         testing::kSystemTime, testing::kGpsRawInt,
                                            testing::kAttitudeQuaternion, testing::kGlobalPositionInt,
                                            testing::kCommandLong,       testing::kCommandAck, testing::kStatusText};
  const std::array<std::uint32_t, 8> ids{0, 2, 24, 31, 33, 76, 77, 253};
  std::vector<Bytes> frames;
  std::ranges::transform(hex, std::back_inserter(frames), from_hex);
  ReadCounts counts;
  const std::vector<Frame> read = read_all(concat(frames), &counts);
  ASSERT_EQ(read.size(), ids.size());
  for (std::size_t i = 0; i < ids.size(); ++i) {
    EXPECT_EQ(read[i].message_id, ids.at(i));
    EXPECT_EQ(read[i].sequence, 7);
  }
  EXPECT_EQ(read[0].system, 1);
  EXPECT_EQ(read[5].system, 255);
  EXPECT_EQ(read[5].component, 190);
  EXPECT_EQ(counts.unknown, 0U);
  EXPECT_EQ(counts.rejected_bytes, 0U);
}

TEST(FrameReader, ZeroFillsWhatMavlink2Trimmed) {
  // The rig's COMMAND_ACK carries 10 bytes; the rest of the payload is zero.
  const std::vector<Frame> read = read_all(from_hex(testing::kCommandAck));
  ASSERT_EQ(read.size(), 1U);
  EXPECT_EQ(read[0].payload[9], std::byte{190});
  for (std::size_t i = 10; i < kMaxPayload; ++i) {
    EXPECT_EQ(read[0].payload.at(i), std::byte{0});
  }
}

TEST(FrameReader, ReadsMavlink1) {
  const Bytes payload = testing::payload_of(from_hex(testing::kHeartbeat));
  const std::vector<Frame> read = read_all(frame_v1(0, 50, payload, {.system = 3, .component = 1, .sequence = 9}));
  ASSERT_EQ(read.size(), 1U);
  EXPECT_EQ(read[0].message_id, kHeartbeatId);
  EXPECT_EQ(read[0].system, 3);
  EXPECT_EQ(read[0].sequence, 9);
}

TEST(FrameReader, SkipsTheSignatureOfASignedFrame) {
  const Bytes payload = testing::payload_of(from_hex(testing::kHeartbeat));
  ReadCounts counts;
  const std::vector<Frame> read =
      read_all(concat({frame_v2(0, 50, payload, {}, 0x01), from_hex(testing::kSystemTime)}), &counts);
  ASSERT_EQ(read.size(), 2U);
  EXPECT_EQ(read[1].message_id, kSystemTimeId);
  EXPECT_EQ(counts.rejected_bytes, 0U);
}

TEST(FrameReader, RejectsAnUnknownIncompatibilityFlag) {
  const Bytes payload = testing::payload_of(from_hex(testing::kHeartbeat));
  const Bytes flagged = frame_v2(0, 50, payload, {}, 0x02);
  ReadCounts counts;
  const std::vector<Frame> read = read_all(concat({flagged, from_hex(testing::kSystemTime)}), &counts);
  ASSERT_EQ(read.size(), 1U);
  EXPECT_EQ(read[0].message_id, kSystemTimeId);
  EXPECT_EQ(counts.rejected_bytes, flagged.size());
}

TEST(FrameReader, StepsOverAMessageIcsDoesNotRead) {
  // MISSION_CURRENT (42) from the rig's autopilots.
  const Bytes mission_current = frame_v2(42, 28, from_hex("0300"));
  ReadCounts counts;
  const std::vector<Frame> read =
      read_all(concat({mission_current, from_hex(testing::kHeartbeat), mission_current}), &counts);
  ASSERT_EQ(read.size(), 1U);
  EXPECT_EQ(read[0].message_id, kHeartbeatId);
  EXPECT_EQ(counts.unknown, 2U);
  EXPECT_EQ(counts.rejected_bytes, 0U);
}

TEST(FrameReader, RejectsABadChecksumAndFindsTheNextFrame) {
  Bytes corrupt = from_hex(testing::kGlobalPositionInt);
  corrupt[12] ^= std::byte{0x01};
  ReadCounts counts;
  const std::vector<Frame> read = read_all(concat({corrupt, from_hex(testing::kHeartbeat)}), &counts);
  ASSERT_EQ(read.size(), 1U);
  EXPECT_EQ(read[0].message_id, kHeartbeatId);
  EXPECT_EQ(counts.rejected_bytes, corrupt.size());
}

TEST(FrameReader, RejectsFramesCutShort) {
  const Bytes whole = from_hex(testing::kGlobalPositionInt);
  ReadCounts counts;
  EXPECT_TRUE(read_all(Bytes(whole.begin(), whole.end() - 1), &counts).empty());
  EXPECT_EQ(counts.rejected_bytes, whole.size() - 1);
  // Shorter than either header.
  EXPECT_TRUE(read_all(from_hex("fd0100000701"), &counts).empty());
  EXPECT_EQ(counts.rejected_bytes, 6U);
  EXPECT_TRUE(read_all(from_hex("fe0107"), &counts).empty());
  EXPECT_EQ(counts.rejected_bytes, 3U);
}

TEST(FrameReader, SkipsNoiseBetweenFrames) {
  ReadCounts counts;
  const std::vector<Frame> read =
      read_all(concat({from_hex("00ff55"), from_hex(testing::kHeartbeat), from_hex("aa")}), &counts);
  ASSERT_EQ(read.size(), 1U);
  EXPECT_EQ(counts.rejected_bytes, 4U);
  EXPECT_TRUE(read_all({}).empty());
}

}  // namespace
}  // namespace ics::mavlink
