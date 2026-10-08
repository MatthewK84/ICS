#include "ics/store/thrift.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "ics/common/check.hpp"

namespace ics::store {

namespace {

constexpr std::uint8_t kStop = 0;
constexpr std::uint64_t kVarintLowBits = 0x7FU;
constexpr std::uint8_t kVarintMore = 0x80U;
constexpr unsigned kVarintShift = 7;
// A field id delta or list size this large takes the long form.
constexpr int kShortLimit = 15;
constexpr std::uint8_t kLongListSize = 0xF0U;
constexpr unsigned kNibble = 4;

[[nodiscard]] std::uint64_t zigzag(const std::int64_t value) noexcept {
  // All ones for a negative value, else zero.
  const std::uint64_t sign = -static_cast<std::uint64_t>(value < 0);
  return (static_cast<std::uint64_t>(value) << 1U) ^ sign;
}

}  // namespace

void append_varint(std::uint64_t value, std::vector<std::byte>& out) {
  // Each pass takes 7 bits, so 64 bits take at most 9 passes.
  while (value > kVarintLowBits) {
    out.push_back(static_cast<std::byte>((value & kVarintLowBits) | kVarintMore));
    value >>= kVarintShift;
  }
  out.push_back(static_cast<std::byte>(value));
}

void ThriftWriter::byte(const std::uint8_t value) { out_.push_back(std::byte{value}); }

void ThriftWriter::begin_struct() noexcept {
  static_cast<void>(ics::check(depth_ < kMaxDepth));
  last_ids_.at(depth_ % kMaxDepth) = 0;
  depth_ = std::min(depth_ + 1, kMaxDepth);
}

void ThriftWriter::begin_struct_field(const std::int16_t id) {
  field_header(id, ThriftType::kStruct);
  begin_struct();
}

void ThriftWriter::end_struct() {
  static_cast<void>(ics::check(depth_ > 0));
  byte(kStop);
  depth_ = std::max(depth_, std::size_t{1}) - 1;
}

void ThriftWriter::field_header(const std::int16_t id, const ThriftType type) {
  static_cast<void>(ics::check(depth_ > 0));
  std::int16_t& last = last_ids_.at((depth_ + kMaxDepth - 1) % kMaxDepth);
  const int delta = id - last;
  last = id;
  if (delta > 0 && delta <= kShortLimit) {
    byte(static_cast<std::uint8_t>((static_cast<unsigned>(delta) << kNibble) | static_cast<unsigned>(type)));
    return;
  }
  byte(static_cast<std::uint8_t>(type));
  append_varint(zigzag(id), out_);
}

void ThriftWriter::field_i32(const std::int16_t id, const std::int32_t value) {
  field_header(id, ThriftType::kI32);
  append_varint(zigzag(value), out_);
}

void ThriftWriter::field_i64(const std::int16_t id, const std::int64_t value) {
  field_header(id, ThriftType::kI64);
  append_varint(zigzag(value), out_);
}

void ThriftWriter::field_binary(const std::int16_t id, const std::string_view value) {
  field_header(id, ThriftType::kBinary);
  binary(value);
}

void ThriftWriter::begin_list_field(const std::int16_t id, const ThriftType element, const std::size_t size) {
  static_cast<void>(ics::check(element != ThriftType::kList));
  field_header(id, ThriftType::kList);
  if (size < kShortLimit) {
    byte(static_cast<std::uint8_t>((size << kNibble) | static_cast<unsigned>(element)));
    return;
  }
  byte(static_cast<std::uint8_t>(kLongListSize | static_cast<unsigned>(element)));
  append_varint(size, out_);
}

void ThriftWriter::i32(const std::int32_t value) { append_varint(zigzag(value), out_); }

void ThriftWriter::binary(const std::string_view value) {
  append_varint(value.size(), out_);
  for (const char c : value) {
    byte(static_cast<std::uint8_t>(c));
  }
}

}  // namespace ics::store
