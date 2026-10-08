#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "ics/common/error.hpp"
#include "ics/v1/pli.pb.h"

namespace ics::store {

// The PLI segment log format (ICS-030). A segment is a file that starts with
// the 8-byte magic "ICSPLI1\n", followed by entries, each:
//
//   u32 payload length, little-endian
//   u32 CRC-32C of the kind byte and the payload, little-endian
//   u8  kind: 1 for an ics.v1.PliRecord, 2 for an ics.v1.PliEvent
//   the serialized message
//
// Entries are only appended. A crash can leave the last entry, or the magic,
// part written: a reader stops at the first entry that is incomplete or fails
// its checks, and everything before it is sound.
inline constexpr std::array<std::byte, 8> kSegmentMagic{std::byte{'I'}, std::byte{'C'}, std::byte{'S'},
                                                        std::byte{'P'}, std::byte{'L'}, std::byte{'I'},
                                                        std::byte{'1'}, std::byte{'\n'}};
inline constexpr std::size_t kEntryHeaderBytes = 9;
// The largest payload an entry holds. A PLI message is well under 1 KiB.
inline constexpr std::size_t kMaxPayloadBytes = std::size_t{1} << 20U;
inline constexpr std::size_t kMaxEntryBytes = kEntryHeaderBytes + kMaxPayloadBytes;

// What an entry holds.
enum class EntryKind : std::uint8_t {
  kRecord = 1,  // An ics.v1.PliRecord.
  kEvent = 2,   // An ics.v1.PliEvent.
};

// One entry; the payload is the serialized message, in the caller's bytes.
struct Entry {
  EntryKind kind = EntryKind::kRecord;
  std::span<const std::byte> payload;
};

// What the bytes at a reader's position hold.
enum class Found : std::uint8_t {
  kEntry,       // A whole entry that passes its checks.
  kIncomplete,  // The start of one: more bytes may complete it.
  kInvalid,     // Not an entry: a bad length, kind or CRC.
};

// The result of parse_entry. entry and size are set for kEntry only.
struct Parsed {
  Found found = Found::kIncomplete;
  Entry entry;
  std::size_t size = 0;
};

// Parses the entry at the start of bytes. Allocates nothing.
[[nodiscard]] Parsed parse_entry(std::span<const std::byte> bytes) noexcept;

// Whether bytes are the segment magic (kEntry), a strict prefix of it
// (kIncomplete), or neither (kInvalid). Bytes past the magic are ignored.
[[nodiscard]] Found parse_magic(std::span<const std::byte> bytes) noexcept;

// Appends an entry holding message to out. kInvalidArgument when the message
// is larger than kMaxPayloadBytes, leaving out as it was.
[[nodiscard]] Status append_entry(const v1::PliRecord& message, std::vector<std::byte>& out);
[[nodiscard]] Status append_entry(const v1::PliEvent& message, std::vector<std::byte>& out);

}  // namespace ics::store
