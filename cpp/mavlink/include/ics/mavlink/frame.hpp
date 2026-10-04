#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace ics::mavlink {

// MAVLink framing (ICS-021): the frames in one UDP datagram, MAVLink 1 or 2,
// as an autopilot's own parser reads them. A byte that starts no valid frame
// is skipped and counted, and the search resumes at the next byte. Only the
// frames of the messages ICS reads (messages.hpp) come out: a frame of any
// other message is stepped over by its length, since its checksum cannot be
// checked without that message's CRC_EXTRA.
//
// A signed MAVLink 2 frame is read and its signature skipped: ICS listens on
// a TAP port and holds no signing key. A frame with any other incompatibility
// flag is rejected, as the MAVLink 2 specification requires.

// The largest payload a frame carries.
inline constexpr std::size_t kMaxPayload = 255;

// The X.25 checksum (CRC-16/MCRF4XX) MAVLink uses, continued from crc.
[[nodiscard]] std::uint16_t x25(std::span<const std::byte> data, std::uint16_t crc = 0xFFFF) noexcept;

// The CRC_EXTRA of a message ICS reads, or nothing for any other message.
[[nodiscard]] std::optional<std::uint8_t> crc_extra(std::uint32_t message_id) noexcept;

// One frame of a message ICS reads.
struct Frame {
  std::uint8_t sequence = 0;
  std::uint8_t system = 0;
  std::uint8_t component = 0;
  std::uint32_t message_id = 0;
  // The payload, zero past the bytes the frame carried: MAVLink 2 drops a
  // payload's trailing zeros, and MAVLink 1 never sends extension fields.
  std::array<std::byte, kMaxPayload> payload{};
};

// What a datagram held besides the frames read from it.
struct ReadCounts {
  // Frames of messages ICS does not read, stepped over.
  std::size_t unknown = 0;
  // Bytes that started no valid frame: noise, a frame cut short, a bad
  // checksum or an unknown incompatibility flag.
  std::size_t rejected_bytes = 0;
};

// Reads the frames of one datagram in order. It refers to the datagram's
// bytes, which must outlive it.
class FrameReader {
 public:
  explicit FrameReader(std::span<const std::byte> datagram) noexcept : data_(datagram) {}

  // The next frame, or nothing once the datagram is used up.
  [[nodiscard]] std::optional<Frame> next() noexcept;

  [[nodiscard]] const ReadCounts& counts() const noexcept { return counts_; }

 private:
  std::span<const std::byte> data_;
  std::size_t offset_ = 0;
  ReadCounts counts_;
};

}  // namespace ics::mavlink
