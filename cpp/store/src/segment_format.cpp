#include "ics/store/segment_format.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <google/protobuf/message_lite.h>

#include "ics/common/check.hpp"
#include "ics/store/crc32c.hpp"

namespace ics::store {

namespace {

constexpr std::size_t kLengthAt = 0;
constexpr std::size_t kCrcAt = 4;
constexpr std::size_t kKindAt = 8;
constexpr unsigned kBitsPerByte = 8;
constexpr std::size_t kWordBytes = 4;

[[nodiscard]] std::uint32_t read_u32(const std::span<const std::byte> bytes) noexcept {
  std::uint32_t value = 0;
  for (std::size_t index = 0; index < kWordBytes; ++index) {
    value |= std::to_integer<std::uint32_t>(bytes[index]) << (kBitsPerByte * index);
  }
  return value;
}

void write_u32(const std::uint32_t value, const std::span<std::byte> bytes) noexcept {
  for (std::size_t index = 0; index < kWordBytes; ++index) {
    bytes[index] = static_cast<std::byte>(value >> (kBitsPerByte * index));
  }
}

[[nodiscard]] bool known_kind(const std::byte kind) noexcept {
  return kind == std::byte{static_cast<std::uint8_t>(EntryKind::kRecord)} ||
         kind == std::byte{static_cast<std::uint8_t>(EntryKind::kEvent)};
}

[[nodiscard]] Status append_message(const EntryKind kind, const google::protobuf::MessageLite& message,
                                    std::vector<std::byte>& out) {
  const std::size_t length = message.ByteSizeLong();
  if (length > kMaxPayloadBytes) {
    return fail(Error::kInvalidArgument);
  }
  const std::size_t start = out.size();
  out.resize(start + kEntryHeaderBytes + length);
  const std::span<std::byte> entry = std::span(out).subspan(start);
  entry[kKindAt] = std::byte{static_cast<std::uint8_t>(kind)};
  const std::span<std::byte> payload = entry.subspan(kEntryHeaderBytes);
  static_cast<void>(ics::check(message.SerializeToArray(payload.data(), static_cast<int>(length))));
  write_u32(static_cast<std::uint32_t>(length), entry.subspan(kLengthAt, kWordBytes));
  write_u32(crc32c(entry.subspan(kKindAt)), entry.subspan(kCrcAt, kWordBytes));
  return {};
}

}  // namespace

Parsed parse_entry(const std::span<const std::byte> bytes) noexcept {
  if (bytes.size() < kEntryHeaderBytes) {
    return {};
  }
  const std::size_t length = read_u32(bytes.subspan(kLengthAt, kWordBytes));
  if (length > kMaxPayloadBytes || !known_kind(bytes[kKindAt])) {
    return {.found = Found::kInvalid, .entry = {}, .size = 0};
  }
  if (bytes.size() - kEntryHeaderBytes < length) {
    return {};
  }
  const std::size_t size = kEntryHeaderBytes + length;
  static_cast<void>(ics::check(size <= bytes.size()));
  if (crc32c(bytes.subspan(kKindAt, size - kKindAt)) != read_u32(bytes.subspan(kCrcAt, kWordBytes))) {
    return {.found = Found::kInvalid, .entry = {}, .size = 0};
  }
  const auto kind = static_cast<EntryKind>(std::to_integer<std::uint8_t>(bytes[kKindAt]));
  return {.found = Found::kEntry,
          .entry = {.kind = kind, .payload = bytes.subspan(kEntryHeaderBytes, length)},
          .size = size};
}

Found parse_magic(const std::span<const std::byte> bytes) noexcept {
  const std::size_t compared = std::min(bytes.size(), kSegmentMagic.size());
  if (!std::ranges::equal(bytes.first(compared), std::span(kSegmentMagic).first(compared))) {
    return Found::kInvalid;
  }
  return compared == kSegmentMagic.size() ? Found::kEntry : Found::kIncomplete;
}

Status append_entry(const v1::PliRecord& message, std::vector<std::byte>& out) {
  return append_message(EntryKind::kRecord, message, out);
}

Status append_entry(const v1::PliEvent& message, std::vector<std::byte>& out) {
  return append_message(EntryKind::kEvent, message, out);
}

}  // namespace ics::store
