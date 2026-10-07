#pragma once

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

#include "ics/common/check.hpp"

namespace ics::camera::detail {

static_assert(std::endian::native == std::endian::little, "cine files are little endian, as ICS hosts are");

// Whether size bytes from offset lie within a span of total bytes, without
// overflow. A range of no bytes always does.
[[nodiscard]] constexpr bool fits(const std::uint64_t total, const std::uint64_t offset,
                                  const std::uint64_t size) noexcept {
  return size <= total - std::min(offset, total);
}

// Copies a value out of the bytes at an offset the caller has checked. Past
// the end, a check fails and the value keeps what it had.
inline void load(const std::span<const std::byte> bytes, const std::size_t offset,
                 const std::span<std::byte> value) noexcept {
  const std::size_t start = std::min(offset, bytes.size());
  const std::size_t count = std::min(value.size(), bytes.size() - start);
  static_cast<void>(check(count == value.size()));
  std::memcpy(value.data(), bytes.subspan(start, count).data(), count);
}

// Copies a value into the bytes at an offset the caller has made room for.
inline void store(const std::span<std::byte> bytes, const std::size_t offset,
                  const std::span<const std::byte> value) noexcept {
  const std::size_t start = std::min(offset, bytes.size());
  const std::size_t count = std::min(value.size(), bytes.size() - start);
  static_cast<void>(check(count == value.size()));
  std::memcpy(bytes.subspan(start, count).data(), value.data(), count);
}

[[nodiscard]] inline std::uint16_t u16(const std::span<const std::byte> bytes, const std::size_t offset) noexcept {
  std::uint16_t value = 0;
  load(bytes, offset, std::as_writable_bytes(std::span(&value, 1)));
  return value;
}

[[nodiscard]] inline std::uint32_t u32(const std::span<const std::byte> bytes, const std::size_t offset) noexcept {
  std::uint32_t value = 0;
  load(bytes, offset, std::as_writable_bytes(std::span(&value, 1)));
  return value;
}

[[nodiscard]] inline std::int32_t i32(const std::span<const std::byte> bytes, const std::size_t offset) noexcept {
  std::int32_t value = 0;
  load(bytes, offset, std::as_writable_bytes(std::span(&value, 1)));
  return value;
}

[[nodiscard]] inline std::uint64_t u64(const std::span<const std::byte> bytes, const std::size_t offset) noexcept {
  std::uint64_t value = 0;
  load(bytes, offset, std::as_writable_bytes(std::span(&value, 1)));
  return value;
}

inline void put_u16(const std::span<std::byte> bytes, const std::size_t offset, const std::uint16_t value) noexcept {
  store(bytes, offset, std::as_bytes(std::span(&value, 1)));
}

inline void put_u32(const std::span<std::byte> bytes, const std::size_t offset, const std::uint32_t value) noexcept {
  store(bytes, offset, std::as_bytes(std::span(&value, 1)));
}

inline void put_i32(const std::span<std::byte> bytes, const std::size_t offset, const std::int32_t value) noexcept {
  store(bytes, offset, std::as_bytes(std::span(&value, 1)));
}

inline void put_u64(const std::span<std::byte> bytes, const std::size_t offset, const std::uint64_t value) noexcept {
  store(bytes, offset, std::as_bytes(std::span(&value, 1)));
}

}  // namespace ics::camera::detail
