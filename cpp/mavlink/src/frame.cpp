#include "ics/mavlink/frame.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <optional>
#include <span>

#include "ics/common/check.hpp"

namespace ics::mavlink {
namespace {

using Bytes = std::span<const std::byte>;

constexpr std::uint8_t kMagicV1 = 0xFE;
constexpr std::uint8_t kMagicV2 = 0xFD;
// A MAVLink 1 header: magic, payload length, sequence, system, component and
// an 8-bit message ID.
constexpr std::size_t kHeaderV1 = 6;
constexpr std::size_t kSequenceAtV1 = 2;
constexpr std::size_t kMessageIdAtV1 = 5;
// A MAVLink 2 header: magic, payload length, incompatibility and
// compatibility flags, sequence, system, component and a 24-bit message ID.
constexpr std::size_t kHeaderV2 = 10;
constexpr std::size_t kIncompatibleAt = 2;
constexpr std::size_t kSequenceAtV2 = 4;
constexpr std::size_t kMessageIdAtV2 = 7;
constexpr std::size_t kLengthAt = 1;
constexpr std::size_t kChecksumSize = 2;
constexpr std::size_t kSignatureSize = 13;
constexpr unsigned kSigned = 0x01U;
constexpr unsigned kBitsPerByte = 8;
constexpr unsigned kByteMask = 0xFFU;
constexpr unsigned kHighNibbleShift = 4;
constexpr unsigned kX25Shift = 3;

struct Extra {
  std::uint32_t message_id = 0;
  std::uint8_t crc_extra = 0;
};

// The CRC_EXTRA of each message ICS reads, from common.xml as mavgen computes
// it: HEARTBEAT, SYSTEM_TIME, GPS_RAW_INT, ATTITUDE_QUATERNION,
// GLOBAL_POSITION_INT, COMMAND_LONG, COMMAND_ACK and STATUSTEXT.
constexpr std::array<Extra, 8> kExtras{
    {{0, 50}, {2, 137}, {24, 24}, {31, 246}, {33, 104}, {76, 152}, {77, 143}, {253, 83}}};

enum class Outcome : std::uint8_t { kFrame, kUnknown, kRejected };

// What the bytes at one position held, and how many of them it took.
struct Step {
  Outcome outcome = Outcome::kRejected;
  std::size_t consumed = 1;
};

constexpr Step kRejectOne{};

[[nodiscard]] unsigned u8(const Bytes data, const std::size_t at) noexcept {
  return std::to_integer<unsigned>(data[at]);
}

[[nodiscard]] std::uint16_t x25_byte(const std::uint16_t crc, const std::byte byte) noexcept {
  unsigned mixed = std::to_integer<unsigned>(byte) ^ (crc & kByteMask);
  mixed = (mixed ^ (mixed << kHighNibbleShift)) & kByteMask;
  return static_cast<std::uint16_t>((static_cast<unsigned>(crc) >> kBitsPerByte) ^ (mixed << kBitsPerByte) ^
                                    (mixed << kX25Shift) ^ (mixed >> kHighNibbleShift));
}

// The frame after a header of header_size bytes at the start of data, if it
// fits, with trailer bytes after its checksum, and its checksum holds.
[[nodiscard]] Step checked_step(const Bytes data, const std::size_t header_size, const std::size_t trailer,
                                Frame& frame) noexcept {
  const std::size_t length = u8(data, kLengthAt);
  const std::size_t checksum_at = header_size + length;
  const std::size_t end = checksum_at + kChecksumSize + trailer;
  if (end > data.size()) {
    return kRejectOne;
  }
  const std::optional<std::uint8_t> extra = crc_extra(frame.message_id);
  if (!extra) {
    return Step{Outcome::kUnknown, end};
  }
  const std::uint16_t crc = x25_byte(x25(data.subspan(kLengthAt, checksum_at - kLengthAt)), std::byte{*extra});
  const unsigned received = u8(data, checksum_at) | (u8(data, checksum_at + 1) << kBitsPerByte);
  if (crc != received) {
    return kRejectOne;
  }
  static_cast<void>(check(length <= frame.payload.size()));
  std::ranges::copy(data.subspan(header_size, length), frame.payload.begin());
  return Step{Outcome::kFrame, end};
}

[[nodiscard]] Step v2_step(const Bytes data, Frame& frame) noexcept {
  if (data.size() < kHeaderV2) {
    return kRejectOne;
  }
  const unsigned incompatible = u8(data, kIncompatibleAt);
  if ((incompatible & ~kSigned) != 0U) {
    return kRejectOne;
  }
  frame.sequence = static_cast<std::uint8_t>(u8(data, kSequenceAtV2));
  frame.system = static_cast<std::uint8_t>(u8(data, kSequenceAtV2 + 1));
  frame.component = static_cast<std::uint8_t>(u8(data, kSequenceAtV2 + 2));
  frame.message_id = u8(data, kMessageIdAtV2) | (u8(data, kMessageIdAtV2 + 1) << kBitsPerByte) |
                     (u8(data, kMessageIdAtV2 + 2) << (2 * kBitsPerByte));
  const std::size_t trailer = (incompatible & kSigned) != 0U ? kSignatureSize : 0;
  return checked_step(data, kHeaderV2, trailer, frame);
}

[[nodiscard]] Step v1_step(const Bytes data, Frame& frame) noexcept {
  if (data.size() < kHeaderV1) {
    return kRejectOne;
  }
  frame.sequence = static_cast<std::uint8_t>(u8(data, kSequenceAtV1));
  frame.system = static_cast<std::uint8_t>(u8(data, kSequenceAtV1 + 1));
  frame.component = static_cast<std::uint8_t>(u8(data, kSequenceAtV1 + 2));
  frame.message_id = u8(data, kMessageIdAtV1);
  return checked_step(data, kHeaderV1, 0, frame);
}

// The frame, unknown frame or rejected byte at the start of data.
[[nodiscard]] Step step_at(const Bytes data, Frame& frame) noexcept {
  const unsigned magic = u8(data, 0);
  if (magic == kMagicV2) {
    return v2_step(data, frame);
  }
  if (magic == kMagicV1) {
    return v1_step(data, frame);
  }
  return kRejectOne;
}

}  // namespace

std::uint16_t x25(const std::span<const std::byte> data, const std::uint16_t crc) noexcept {
  return std::accumulate(data.begin(), data.end(), crc, x25_byte);
}

std::optional<std::uint8_t> crc_extra(const std::uint32_t message_id) noexcept {
  const auto* const found =
      std::ranges::find_if(kExtras, [message_id](const Extra& extra) { return extra.message_id == message_id; });
  if (found == kExtras.end()) {
    return std::nullopt;
  }
  return found->crc_extra;
}

std::optional<Frame> FrameReader::next() noexcept {
  while (offset_ < data_.size()) {
    Frame frame;
    const Step step = step_at(data_.subspan(offset_), frame);
    static_cast<void>(check(step.consumed <= data_.size() - offset_));
    offset_ += step.consumed;
    if (step.outcome == Outcome::kFrame) {
      return frame;
    }
    if (step.outcome == Outcome::kUnknown) {
      ++counts_.unknown;
    } else {
      counts_.rejected_bytes += step.consumed;
    }
  }
  return std::nullopt;
}

}  // namespace ics::mavlink
