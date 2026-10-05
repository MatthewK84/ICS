#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

namespace ics::flightlog::testing {

// Writes DataFlash logs for tests, message by message.
class DataFlashBuilder {
 public:
  template <typename T>
  static void put(std::vector<std::byte>& out, const T value) {
    std::array<std::byte, sizeof(T)> encoded{};
    std::memcpy(encoded.data(), &value, sizeof(T));
    out.insert(out.end(), encoded.begin(), encoded.end());
  }

  // Text in a fixed-size field, padded with NULs.
  static void put_text(std::vector<std::byte>& out, const std::string_view text, const std::size_t size) {
    for (std::size_t i = 0; i < size; ++i) {
      out.push_back(i < text.size() ? static_cast<std::byte>(text[i]) : std::byte{0});
    }
  }

  DataFlashBuilder& message(const std::uint8_t type, const std::vector<std::byte>& body) {
    bytes_.push_back(std::byte{0xA3});
    bytes_.push_back(std::byte{0x95});
    bytes_.push_back(static_cast<std::byte>(type));
    bytes_.insert(bytes_.end(), body.begin(), body.end());
    return *this;
  }

  DataFlashBuilder& format(const std::uint8_t type, const std::uint8_t length, const std::string_view name,
                           const std::string_view format, const std::string_view columns) {
    std::vector<std::byte> fields;
    put(fields, type);
    put(fields, length);
    put_text(fields, name, 4);
    put_text(fields, format, 16);
    put_text(fields, columns, 64);
    return message(128, fields);
  }

  DataFlashBuilder& raw(const std::vector<std::byte>& bytes) {
    bytes_.insert(bytes_.end(), bytes.begin(), bytes.end());
    return *this;
  }

  [[nodiscard]] std::vector<std::byte>& bytes() { return bytes_; }
  [[nodiscard]] const std::vector<std::byte>& bytes() const { return bytes_; }

 private:
  std::vector<std::byte> bytes_;
};

// The bytes of values, in order.
template <typename... T>
std::vector<std::byte> body(const T... values) {
  std::vector<std::byte> out;
  (DataFlashBuilder::put(out, values), ...);
  return out;
}

}  // namespace ics::flightlog::testing
