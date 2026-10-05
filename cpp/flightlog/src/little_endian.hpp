#pragma once

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstring>
#include <iterator>
#include <optional>
#include <span>
#include <string>

namespace ics::flightlog::detail {

static_assert(std::endian::native == std::endian::little, "flight logs are little endian, as ICS hosts are");

// A value stored at an offset of a byte span, or nothing when it would run
// past the end.
template <typename T>
[[nodiscard]] std::optional<T> read(const std::span<const std::byte> bytes, const std::size_t offset) noexcept {
  if (offset > bytes.size() || bytes.size() - offset < sizeof(T)) {
    return std::nullopt;
  }
  T value{};
  std::memcpy(&value, bytes.subspan(offset, sizeof(T)).data(), sizeof(T));
  return value;
}

// The text in a span, up to its first NUL.
[[nodiscard]] inline std::string text(const std::span<const std::byte> bytes) {
  const auto end = std::ranges::find(bytes, std::byte{0});
  std::string out;
  std::ranges::transform(bytes.begin(), end, std::back_inserter(out),
                         [](const std::byte b) { return static_cast<char>(b); });
  return out;
}

}  // namespace ics::flightlog::detail
